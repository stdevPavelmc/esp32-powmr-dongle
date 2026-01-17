#!/usr/bin/env python3

import configparser
import sys
import time
import logging
import json
import requests
from datetime import datetime
from threading import Thread, Event

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class DataBridge:
    def __init__(self, config_path):
        self.config = configparser.ConfigParser()
        self.config.read(config_path)

        self.mqtt_client = None
        self.influx_client = None
        self.write_api = None
        self.topics = []
        self.stop_event = Event()

        # Track current values: {measurement.field: value}
        self.current_values = {}
        self.initialized = False

        # Get source type from config
        self.source_type = self.config.get('source', 'type', fallback='mqtt')
        
        self._setup_influxdb()
        
        if self.source_type == 'mqtt':
            self._setup_mqtt()
        elif self.source_type == 'http':
            self._setup_http()
        else:
            logger.error(f"Invalid source type: {self.source_type}")
            sys.exit(1)

        self.logging = True

    def _setup_influxdb(self):
        """Initialize InfluxDB client"""
        try:
            url = self.config.get('influxdb', 'url')
            token = self.config.get('influxdb', 'token')
            org = self.config.get('influxdb', 'org')

            self.influx_client = InfluxDBClient(
                url=url, 
                token=token, 
                org=org,
                timeout=15000
            )

            self.write_api = self.influx_client.write_api(write_options=SYNCHRONOUS)
            self.bucket = self.config.get('influxdb', 'bucket')
            self.org = org

            logger.info(f"InfluxDB client initialized: {url}")
        except Exception as e:
            logger.error(f"Failed to initialize InfluxDB: {e}")
            sys.exit(1)

    def _setup_http(self):
        """Initialize HTTP polling settings"""
        try:
            self.http_url = self.config.get('http', 'url')
            self.http_poll_interval = self.config.getint('http', 'poll_interval', fallback=15)
            logger.info(f"HTTP source initialized: {self.http_url}")
        except Exception as e:
            logger.error(f"Failed to initialize HTTP: {e}")
            sys.exit(1)

    def _setup_mqtt(self):
        """Initialize MQTT client"""
        try:
            client_id = self.config.get('mqtt', 'client_id', fallback='mqtt_influx_bridge')
            self.mqtt_client = mqtt.Client(client_id=client_id)

            username = self.config.get('mqtt', 'username', fallback='')
            password = self.config.get('mqtt', 'password', fallback='')

            if username and password:
                self.mqtt_client.username_pw_set(username, password)

            self.mqtt_client.on_connect = self._on_connect
            self.mqtt_client.on_message = self._on_message
            self.mqtt_client.on_disconnect = self._on_disconnect

            # Parse topics
            topics_raw = self.config.get('topics', 'topics')
            self.topics = [t.strip() for t in topics_raw.split('\n') if t.strip()]

            logger.info(f"MQTT client initialized with {len(self.topics)} topics")
        except Exception as e:
            logger.error(f"Failed to initialize MQTT: {e}")
            sys.exit(1)

    def _on_connect(self, client, userdata, flags, rc):
        """MQTT connection callback"""
        if rc == 0:
            logger.info("Connected to MQTT broker")
            for topic in self.topics:
                client.subscribe(topic)
                logger.info(f"Subscribed to: {topic}")
        else:
            logger.error(f"MQTT connection failed with code {rc}")

    def _on_disconnect(self, client, userdata, rc):
        """MQTT disconnection callback"""
        if rc != 0:
            logger.warning(f"Unexpected MQTT disconnect: {rc}")

    def _on_message(self, client, userdata, msg):
        """Handle incoming MQTT messages"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')

            logger.debug(f"Received: {topic} = {payload}")

            # Parse topic: /powmr/inverter.temp -> measurement=inverter, field=temp
            parts = topic.strip('/').split('/')
            if len(parts) < 2:
                logger.warning(f"Invalid topic format: {topic}")
                return

            # Last part contains measurement.field
            last_part = parts[-1]
            if '.' not in last_part:
                logger.warning(f"Invalid topic format (no dot): {topic}")
                return

            measurement, field = last_part.split('.', 1)

            # Convert payload to float
            try:
                value = float(payload)
            except ValueError:
                logger.warning(f"Cannot convert to float: {payload} for {topic}")
                return

            # Check and write if changed
            self._check_and_write(measurement, field, value)

        except Exception as e:
            logger.error(f"Error processing message: {e}")

    def _poll_http(self):
        """Poll HTTP endpoint for data"""
        while not self.stop_event.is_set():
            try:
                response = requests.get(self.http_url, timeout=10)
                response.raise_for_status()
                data = response.json()

                # Check if data is valid
                if data.get('inverter', {}).get('valid_info') != 1:
                    logger.warning("Invalid data from HTTP endpoint (valid_info != 1)")
                    time.sleep(self.http_poll_interval)
                    continue

                # Get read_time_mean and round to next 5 second multiple
                read_time_mean_ms = data.get('inverter', {}).get('read_time_mean', self.http_poll_interval * 1000)
                read_time_s = read_time_mean_ms / 1000.0
                
                # Round up to next 5 second multiple
                next_poll = ((int(read_time_s) + 4) // 5) * 5
                
                logger.debug(f"read_time_mean: {read_time_s:.1f}s -> polling every {next_poll}s")

                # Flatten and write data
                self._process_json_data(data)

                # Mark as initialized after first successful read
                if not self.initialized:
                    self.initialized = True
                    logger.info("Data initialized from HTTP source")

                # Wait for next poll
                self.stop_event.wait(next_poll)

            except requests.exceptions.RequestException as e:
                logger.error(f"HTTP request failed: {e}")
                self.stop_event.wait(self.http_poll_interval)
            except Exception as e:
                logger.error(f"Error polling HTTP endpoint: {e}")
                self.stop_event.wait(self.http_poll_interval)

    def _process_json_data(self, data, prefix=''):
        """Recursively process JSON data and write to InfluxDB"""
        for key, value in data.items():
            if isinstance(value, dict):
                # Recursively process nested objects
                new_prefix = f"{prefix}{key}." if prefix else f"{key}."
                self._process_json_data(value, new_prefix)
            elif isinstance(value, (int, float)):
                # Split prefix to get measurement (last part before final field)
                if prefix:
                    parts = prefix.rstrip('.').split('.')
                    if len(parts) >= 1:
                        measurement = parts[-1]
                        field = key
                        self._check_and_write(measurement, field, float(value))

    def _check_and_write(self, measurement, field, value):
        """Check if value changed and write to InfluxDB"""
        key = f"{measurement}.{field}"
        
        # Get previous value (None if first time)
        prev_value = self.current_values.get(key)
        
        # Determine if we should write
        should_write = False
        
        if not self.initialized:
            # During initialization, store all values but don't write zeros
            self.current_values[key] = value
            if value != 0:
                should_write = True
                logger.debug(f"Init: {key} = {value}")
        else:
            # After initialization, only write if value changed
            if prev_value is None:
                # New measurement appeared
                self.current_values[key] = value
                if value != 0:
                    should_write = True
                    logger.debug(f"New: {key} = {value}")
            elif prev_value != value:
                # Value changed
                self.current_values[key] = value
                should_write = True
                logger.debug(f"Changed: {key} = {prev_value} -> {value}")
        
        # Write to InfluxDB if needed
        if should_write:
            self._write_to_influx(measurement, field, value)

    def _write_to_influx(self, measurement, field, value):
        """Write data point to InfluxDB"""
        try:
            point = Point(measurement).field(field, value).time(datetime.utcnow())
            self.write_api.write(bucket=self.bucket, org=self.org, record=point)
            if self.logging:
                logger.info(f"Written to InfluxDB: {measurement}.{field} = {value}")
        except Exception as e:
            logger.error(f"Failed to write to InfluxDB: {e}")

    def run(self):
        """Start the bridge"""
        try:
            if self.source_type == 'mqtt':
                host = self.config.get('mqtt', 'host')
                port = self.config.getint('mqtt', 'port')

                logger.info(f"Connecting to MQTT broker: {host}:{port}")
                self.mqtt_client.connect(host, port, 60)
                
                # Mark as initialized after connecting to MQTT
                # (we'll populate values as messages arrive)
                self.initialized = False
                
                self.mqtt_client.loop_forever()
            
            elif self.source_type == 'http':
                logger.info("Starting HTTP polling...")
                self._poll_http()

        except KeyboardInterrupt:
            logger.info("Shutting down...")
            self.shutdown()
        except Exception as e:
            logger.error(f"Runtime error: {e}")
            self.shutdown()
            sys.exit(1)

    def shutdown(self):
        """Clean shutdown"""
        self.stop_event.set()
        if self.mqtt_client:
            self.mqtt_client.disconnect()
        if self.influx_client:
            self.influx_client.close()
        logger.info(f"Bridge stopped. Tracked {len(self.current_values)} measurements")

if __name__ == '__main__':
    config_path = '/etc/mqtt_influx_bridge/config.ini'

    bridge = DataBridge(config_path)
    bridge.run()
