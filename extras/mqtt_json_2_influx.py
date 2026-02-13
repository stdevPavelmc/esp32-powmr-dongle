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
        self.cloud_influx_client = None
        self.cloud_write_api = None
        self.topics = []
        self.stop_event = Event()

        # Track current values: {measurement.field: value}
        self.current_values = {}
        # Track last write time: {measurement.field: timestamp}
        self.last_write_time = {}
        # Track last successful read time: {measurement.field: timestamp}
        self.last_read_time = {}
        self.initialized = False
        
        # Heartbeat interval (60 seconds)
        self.heartbeat_interval = 60

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

        self.logging = False
        
        # Start heartbeat thread
        self.heartbeat_thread = Thread(target=self._heartbeat_loop, daemon=True)
        self.heartbeat_thread.start()

    def _setup_influxdb(self):
        """Initialize InfluxDB clients (local primary and cloud secondary)"""
        try:
            # Primary (local) InfluxDB
            local_url = self.config.get('influxdb', 'url')
            local_token = self.config.get('influxdb', 'token')
            local_org = self.config.get('influxdb', 'org')

            self.influx_client = InfluxDBClient(
                url=local_url,
                token=local_token,
                org=local_org,
                timeout=15000
            )

            self.write_api = self.influx_client.write_api(write_options=SYNCHRONOUS)
            self.bucket = self.config.get('influxdb', 'bucket')
            self.org = local_org

            logger.info(f"Local InfluxDB client initialized: {local_url}")

            # Secondary (cloud) InfluxDB
            cloud_url = self.config.get('influxdb_cloud', 'url', fallback=None)
            if cloud_url:
                cloud_token = self.config.get('influxdb_cloud', 'token')
                cloud_org = self.config.get('influxdb_cloud', 'org')
                cloud_bucket = self.config.get('influxdb_cloud', 'bucket', fallback=self.bucket)

                self.cloud_influx_client = InfluxDBClient(
                    url=cloud_url,
                    token=cloud_token,
                    org=cloud_org,
                    timeout=30000  # Longer timeout for cloud
                )

                self.cloud_write_api = self.cloud_influx_client.write_api(write_options=SYNCHRONOUS)
                self.cloud_bucket = cloud_bucket
                self.cloud_org = cloud_org

                logger.info(f"Cloud InfluxDB client initialized: {cloud_url}")

                # Start cloud sync thread
                self.cloud_sync_thread = Thread(target=self._cloud_sync_loop, daemon=True)
                self.cloud_sync_thread.start()
            else:
                self.cloud_influx_client = None
                self.cloud_write_api = None
                logger.info("No cloud InfluxDB configuration found - running local only")

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
                read_time_s = data.get('inverter', {}).get('read_time_mean', self.http_poll_interval)
                
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

        # Update last read time for successful reads
        self.last_read_time[key] = time.time()

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

    def _heartbeat_loop(self):
        """Periodically write unchanged values to maintain data continuity"""
        logger.info(f"Heartbeat thread started (interval: {self.heartbeat_interval}s)")

        while not self.stop_event.is_set():
            self.stop_event.wait(self.heartbeat_interval)

            if self.stop_event.is_set():
                break

            if not self.initialized:
                continue

            now = time.time()

            # Write current values that have been successfully read recently and haven't been written recently
            for key, value in list(self.current_values.items()):
                last_write = self.last_write_time.get(key, 0)
                last_read = self.last_read_time.get(key, 0)

                # Only heartbeat if:
                # 1. Value was successfully read within the last 2 minutes (to ensure it's not stale)
                # 2. More than heartbeat_interval has passed since last write
                # 3. Value is not zero (zeros are only sent once, not in heartbeat)
                if (now - last_read < 120 and  # Successfully read within last 2 minutes
                    now - last_write >= self.heartbeat_interval and
                    value != 0):  # Don't heartbeat zero values
                    measurement, field = key.split('.', 1)
                    self._write_to_influx(measurement, field, value, is_heartbeat=True)

    def _write_to_influx(self, measurement, field, value, is_heartbeat=False):
        """Write data point to InfluxDB (local primary and cloud secondary)"""
        key = f"{measurement}.{field}"
        point = Point(measurement).field(field, value).time(datetime.utcnow())

        # Write to local (primary) InfluxDB
        try:
            self.write_api.write(bucket=self.bucket, org=self.org, record=point)

            if self.logging:
                log_type = "Heartbeat" if is_heartbeat else "Written"
                logger.info(f"{log_type} to local InfluxDB: {measurement}.{field} = {value}")
        except Exception as e:
            logger.error(f"Failed to write to local InfluxDB: {e}")
            return  # Don't update timestamps if local write fails

        # Write to cloud (secondary) InfluxDB if configured
        if self.cloud_write_api:
            try:
                self.cloud_write_api.write(bucket=self.cloud_bucket, org=self.cloud_org, record=point)

                if self.logging:
                    log_type = "Heartbeat" if is_heartbeat else "Written"
                    logger.debug(f"{log_type} to cloud InfluxDB: {measurement}.{field} = {value}")
            except Exception as e:
                logger.warning(f"Failed to write to cloud InfluxDB (continuing): {e}")
                # Don't fail the operation, just log the warning

        # Update last write time only after successful local write
        self.last_write_time[key] = time.time()

    def _cloud_sync_loop(self):
        """Periodically check cloud connectivity and sync missing data"""
        logger.info("Cloud sync thread started (checking every 30 minutes)")

        while not self.stop_event.is_set():
            self.stop_event.wait(1800)  # Check every 30 minutes

            if self.stop_event.is_set():
                break

            if not self.initialized or not self.cloud_influx_client:
                continue

            try:
                # Check cloud connectivity by querying recent data
                self._sync_missing_data()
            except Exception as e:
                logger.warning(f"Cloud sync failed: {e}")

    def _sync_missing_data(self):
        """Compare local and cloud data for the last 6 hours and sync missing points"""
        try:
            # Query local data from the last 6 hours
            local_query = f'''
            from(bucket: "{self.bucket}")
                |> range(start: -6h)
                |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
            '''

            local_tables = self.influx_client.query_api().query(local_query, org=self.org)

            if not local_tables:
                logger.debug("No local data found in the last 6 hours")
                return

            # Group data by measurement and time
            local_data = {}
            for table in local_tables:
                for record in table.records:
                    key = f"{record.get_measurement()}_{record.get_time()}"
                    local_data[key] = {
                        'measurement': record.get_measurement(),
                        'time': record.get_time(),
                        'fields': record.values
                    }

            # Query cloud data from the last 6 hours
            cloud_query = f'''
            from(bucket: "{self.cloud_bucket}")
                |> range(start: -6h)
                |> pivot(rowKey:["_time"], columnKey: ["_field"], valueColumn: "_value")
            '''

            cloud_tables = self.cloud_influx_client.query_api().query(cloud_query, org=self.cloud_org)

            # Group cloud data by measurement and time
            cloud_data = {}
            if cloud_tables:
                for table in cloud_tables:
                    for record in table.records:
                        key = f"{record.get_measurement()}_{record.get_time()}"
                        cloud_data[key] = record.values

            # Find missing data (exists in local but not in cloud)
            missing_count = 0
            for key, local_record in local_data.items():
                if key not in cloud_data:
                    # This data point is missing from cloud, send it
                    measurement = local_record['measurement']
                    timestamp = local_record['time']

                    # Create points for each field
                    for field_name, value in local_record['fields'].items():
                        if field_name not in ['_time', '_measurement', 'result', 'table']:
                            try:
                                point = Point(measurement).field(field_name, value).time(timestamp)
                                self.cloud_write_api.write(
                                    bucket=self.cloud_bucket,
                                    org=self.cloud_org,
                                    record=point
                                )
                                missing_count += 1
                            except Exception as e:
                                logger.warning(f"Failed to sync point to cloud: {e}")

            if missing_count > 0:
                logger.info(f"Synced {missing_count} missing data points to cloud InfluxDB")
            else:
                logger.debug("Cloud data is up to date")

        except Exception as e:
            logger.warning(f"Failed to sync missing data to cloud: {e}")

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
        if self.cloud_influx_client:
            self.cloud_influx_client.close()
        logger.info(f"Bridge stopped. Tracked {len(self.current_values)} measurements")

if __name__ == '__main__':
    config_path = '/etc/mqtt_influx_bridge/config.ini'

    bridge = DataBridge(config_path)
    bridge.run()
