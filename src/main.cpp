
// all includes
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <WebSerial.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SimpleTimer.h>          // Simple Task Time Manager
#include <PubSubClient.h>

// local config on files
#include "wifi.h"
#include "mqtt.h"

/********* Configurable flags *************/
#define WEBSERIAL 1
#define VERBOSE_SERIAL 1

#define VERSION             1.04

// rewrite prints
#ifdef WEBSERIAL
  #define sprint(x) WebSerial.print(x)
  #define sprintln(x) WebSerial.println(x)
#else
  #define sprint(x) Serial.print(x)
  #define sprintln(x) Serial.println(x)
#endif

// enable or disable pubsub features
#define PUBSUB

// simple timer
SimpleTimer timer;

// modbus data
ModbusMaster node;
#define MBUS_REGISTERS 61 // Words uint16, starting from 4501 to 4562
#define CHUNK_SIZE 3
#define RETRY_COUNT 2
#define CHUNK_DELAY_US 100
uint16_t mbus_data[MBUS_REGISTERS + 1]; // allow for room

// webserver related
AsyncWebServer server(80);
IPAddress myIP;

#ifdef PUBSUB
  // MQTT data
  WiFiClient espClient;
  PubSubClient psclient(espClient);
  unsigned long lastMsg = 0;
  #define MSG_BUFFER_SIZE	(10)
  char msg[MSG_BUFFER_SIZE];
  int value = 0;
#endif

// SPIFFS
#define FORMAT_SPIFFS_IF_FAILED true

// Wifi status
bool wifi_mode = 0; // 0 = client | 1 = AP

// define serial connection got SSerial
#define TXD2   GPIO_NUM_17  // TXD2
#define RXD2   GPIO_NUM_16  // RXD2

// define SoftWare Serial
EspSoftwareSerial::UART SSerial;

#define READ_INTERVAL  15000 // 15 seconds, it's millis

// this is the linear part of the range
#define BATT_MAX_VOLTAGE 27
#define BATT_MIN_VOLTAGE 23

// ac data
struct ACData {
  float input_voltage;
  float input_freq;
  float output_voltage;
  float output_freq;
  float output_current;
  float output_va;
  float output_watts;
  float output_load_percent;
};

ACData ac;

// dc data
struct DCData {
  float pv_voltage;
  float pv_power;
  float pv_current;
  float voltage;
  float voltage_;
  float voltage_corrected;
  float charge_current;
  float charge_current_;
  float discharge_current;
  float discharge_current_;
  float charge_power;
  float discharge_power;
  float charged_voltage = 28.8;
  float batt_v_compensation_k = 0.01;
  float new_k = 0;
};

DCData dc;

// inverter data
struct InverterData {
  float temp = 0;
  /* Operational mode
    0:
    1:
    2:
    3: On Battery
    4: On AC
    5:
  */
  uint16_t op_mode;
  /* Charger status
    0: Off
    1: Idle
    2: Active
  */
  uint16_t charger;
  /* FAKE Output source priority
    0: Battery
    1: AC
    2: Solar
  */
  uint16_t output_source_priority;
  /* FAKE charger source priority
    0: Battery
    1: Solar
    2: AC 
  */
  uint16_t charger_source_priority;
  float eff_va = 0;
  float eff_w = 0;
  float soc = 0;
  byte valid_info = 0;
};

InverterData inverter;

#ifdef PUBSUB
void publish(char* topic, float payload) {
  // publish the payload to the specified topic, convert to string all values before publishing
  String payload_str = String(payload);
  psclient.publish(topic, payload_str.c_str());
}
#endif

// Read registers in chunks with retry logic
uint8_t read_registers_chunked(uint16_t start_addr, uint16_t total_regs, uint16_t *data) {
  uint16_t chunks = (total_regs + CHUNK_SIZE - 1) / CHUNK_SIZE;
  uint16_t current_addr = start_addr;
  uint16_t regs_read = 0;
  
  for (uint16_t chunk = 0; chunk < chunks; chunk++) {
    uint16_t regs_to_read = min(CHUNK_SIZE, total_regs - regs_read);
    uint8_t attempts = 0;
    uint8_t success = 0;
    
    // Try reading this chunk (with retry)
    while (attempts <= RETRY_COUNT && !success) {
      uint8_t result = node.readHoldingRegisters(current_addr, regs_to_read);
      
      if (result == node.ku8MBSuccess) {
        // Copy chunk data to buffer
        for (uint16_t j = 0; j < regs_to_read; j++) {
          data[regs_read + j] = node.getResponseBuffer(j);
        }
        success = 1;
        
      } else {
        attempts++;
        // delay between retries
        if (attempts < RETRY_COUNT) {
          delayMicroseconds(CHUNK_DELAY_US);
        }
      }
    }
    
    if (!success) {
      sprint("Failed to read chunk at addr ");
      sprint(current_addr);
      sprint(" after ");
      sprint(attempts);
      sprintln(" attempts");
      return 0;  // Failure
    }
    
    regs_read += regs_to_read;
    current_addr += regs_to_read;
    
    // Delay between chunks (except after last chunk)
    if (chunk < chunks - 1) {
      delayMicroseconds(CHUNK_DELAY_US);
    }
  }
  
  return 1;  // Success
}

void send_request() {
  // Read all registers 4501-4561 (61 registers total)
  sprintln("Reading registers 4501-4561 (61 regs)");
  if (!read_registers_chunked(4501, MBUS_REGISTERS, mbus_data)) {
    sprintln("Error reading registers");
    inverter.valid_info = 0;
    return;
  }
  // valid info is only declared at the end, to get here the last value during the calculations

  // Process data (indexes relative to 4501)
  inverter.op_mode = (float)htons(mbus_data[0]);  // 4501
  #ifdef VERBOSE_SERIAL
    sprint("inverter.op_mode: ");
    sprintln(inverter.op_mode);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.op_mode", inverter.op_mode);
  #endif

  ac.input_voltage = htons(mbus_data[1]) / 10.0;  // 4502
  #ifdef VERBOSE_SERIAL
    sprint("ac.input_voltage: ");
    sprintln(ac.input_voltage);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.input_voltage", ac.input_voltage);
  #endif

  ac.input_freq = htons(mbus_data[2]) / 10.0;  // 4503
  #ifdef VERBOSE_SERIAL
    sprint("ac.input_freq: ");
    sprintln(ac.input_freq);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.input_freq", ac.input_freq);
  #endif

  dc.pv_voltage = htons(mbus_data[3]) / 10.0;  // 4504
  if (dc.pv_voltage < 6) { // 1/10 of the minimum voltage
    dc.pv_voltage = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_voltage: ");
    sprintln(dc.pv_voltage);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.pv_voltage", dc.pv_voltage);
  #endif

  dc.pv_power = (float)htons(mbus_data[4]);  // 4505
  if (dc.pv_voltage < 6) { // 1/10 of the minimum voltage
    dc.pv_power = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_power: ");
    sprintln(dc.pv_power);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.pv_power", dc.pv_power);
  #endif

  // calculated
  if (dc.pv_voltage > 0) { // 1/10 of the minimum voltage
    // valid voltage
    dc.pv_current = dc.pv_power / dc.pv_voltage;
  } else {
    dc.pv_current = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_current: ");
    sprintln(dc.pv_current);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.pv_current", dc.pv_current);
  #endif

  dc.voltage = htons(mbus_data[5]) / 10.0;  // 4506
  #ifdef VERBOSE_SERIAL
    sprint("dc.voltage: ");
    sprintln(dc.voltage);
  #endif
  // will be compensated by the cable losses see at the bottom
  #ifdef PUBSUB
    publish("/powmr/dc.voltage", dc.voltage);
  #endif

  dc.charge_current = (float)htons(mbus_data[7]);  // 4508
  #ifdef VERBOSE_SERIAL
    sprint("dc.charge_current: ");
    sprintln(dc.charge_current);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.charge_current", dc.charge_current);
  #endif

  dc.discharge_current = (float)htons(mbus_data[8]);  // 4509
  #ifdef VERBOSE_SERIAL
    sprint("dc.discharge_current: ");
    sprintln(dc.discharge_current);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.discharge_current", dc.discharge_current);
  #endif

  // calculated
  dc.discharge_power = dc.voltage * dc.discharge_current;
  #ifdef VERBOSE_SERIAL
    sprint("dc.discharge_power: ");
    sprintln(dc.discharge_power);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.discharge_power", dc.discharge_power);
  #endif

  // calculated
  dc.charge_power = dc.voltage * dc.charge_current;
  #ifdef VERBOSE_SERIAL
    sprint("dc.charge_power: ");
    sprintln(dc.charge_power);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.charge_power", dc.charge_power);
  #endif
  
  ac.output_voltage = htons(mbus_data[9]) / 10.0;  // 4510
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_voltage: ");
    sprintln(ac.output_voltage);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.output_voltage", ac.output_voltage);
  #endif

  ac.output_freq = htons(mbus_data[10]) / 10.0;  // 4511
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_freq: ");
    sprintln(ac.output_freq);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.output_freq", ac.output_freq);
  #endif

  ac.output_va = (float)htons(mbus_data[11]);  // 4512
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_va: ");
    sprintln(ac.output_va);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.output_va", ac.output_va);
  #endif

  ac.output_watts = (float)htons(mbus_data[12]);  // 4513
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_watts: ");
    sprintln(ac.output_watts);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.output_watts", ac.output_watts);
  #endif

  ac.output_load_percent = (float)htons(mbus_data[13]);  // 4514
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_load_percent: ");
    sprintln(ac.output_load_percent);
  #endif
  #ifdef PUBSUB
    publish("/powmr/ac.output_load_percent", ac.output_load_percent);
  #endif

  inverter.charger_source_priority = (float)htons(mbus_data[35]);  // 4536
  #ifdef VERBOSE_SERIAL
    sprint("inverter.charger_source_priority: ");
    sprintln(inverter.charger_source_priority);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.charger_source_priority", inverter.charger_source_priority);
  #endif

  inverter.output_source_priority = (float)htons(mbus_data[36]);  // 4537
  #ifdef VERBOSE_SERIAL
    sprint("inverter.output_source_priority: ");
    sprintln(inverter.output_source_priority);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.output_source_priority", inverter.output_source_priority);
  #endif

  inverter.charger = (float)htons(mbus_data[54]);  // 4555
  #ifdef VERBOSE_SERIAL
    sprint("inverter.charger: ");
    sprintln(inverter.charger);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.charger", inverter.charger);
  #endif

  inverter.temp = (float)htons(mbus_data[56]);  // 4557
  #ifdef VERBOSE_SERIAL
    sprint("inverter.temp: ");
    sprintln(inverter.temp);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.temp", inverter.temp);
  #endif

  // Battery voltage compensation calculation
  float charge_current_change = -(dc.discharge_current - dc.discharge_current_) 
                                  + (dc.charge_current - dc.charge_current_);
  if (inverter.valid_info && abs(charge_current_change) > 5.0) {
    dc.new_k = (dc.voltage - dc.voltage_) / charge_current_change;
    dc.batt_v_compensation_k += (dc.new_k - dc.batt_v_compensation_k) * 0.1;
    
    // new compensation factor
    #ifdef VERBOSE_SERIAL
      sprint("dc.new: ");
      sprintln(dc.new_k);
    #endif
    #ifdef PUBSUB
      publish("/powmr/dc.new_k", inverter.temp);
    #endif
  }

  // send compensation
  #ifdef VERBOSE_SERIAL
    sprint("dc.batt_v_compensation_k: ");
    sprintln(dc.batt_v_compensation_k);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.batt_v_compensation_k", dc.batt_v_compensation_k);
  #endif

  // voltage corrected by cable losses
  dc.voltage_corrected = dc.voltage - (dc.batt_v_compensation_k * dc.charge_current)
                                    + (dc.batt_v_compensation_k * dc.discharge_current);
  
  #ifdef VERBOSE_SERIAL
    sprint("dc.voltage_corrected: ");
    sprintln(dc.voltage_corrected);
  #endif
  #ifdef PUBSUB
    publish("/powmr/dc.voltage_corrected", dc.voltage_corrected);
  #endif
  
  // simple soc calculation
  float soc = 100.0 * (dc.voltage_corrected - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE);
  // limit to 0-100
  inverter.soc = (float)constrain(soc, 0, 100);
  #ifdef VERBOSE_SERIAL
    sprint("inverter.soc: ");
    sprintln(inverter.soc);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.soc", inverter.soc);
  #endif

  // call efficiency VA
  float input_power = dc.pv_power + dc.discharge_power;
  if (input_power > 0) {
    inverter.eff_va = (100.0 * ac.output_va) / input_power;
  }
  #ifdef VERBOSE_SERIAL
    sprint("inverter.eff_va: ");
    sprintln(inverter.eff_va);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.eff_va", inverter.eff_va);
  #endif

  // call efficiency WATTS
  if (input_power > 0) {
    inverter.eff_w = (100.0 * ac.output_watts) / input_power;
  }
  #ifdef VERBOSE_SERIAL
    sprint("inverter.eff_w: ");
    sprintln(inverter.eff_w);
  #endif
  #ifdef PUBSUB
    publish("/powmr/inverter.eff_w", inverter.eff_w);
  #endif
  
  
  dc.voltage_ = dc.voltage;
  dc.charge_current_ = dc.charge_current;
  dc.discharge_current_ = dc.discharge_current;
  
  // All reads successful
  // declare valid info
  inverter.valid_info = 1;
  sprintln("All data successfully read");
}

void OTA_setup() {
  ArduinoOTA
      .onStart([]()
       {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
      .onEnd([]()
     { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
          { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
       {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      } });

  // Port defaults to 3232
  ArduinoOTA.setPort(3232);
  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname(hostname);
  // No authentication by default
  // ArduinoOTA.setPassword("admin");
  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
}

void mDNS_setup() {
  if (!MDNS.begin(hostname)) {
    // static const char* message = "Error setting up MDNS responder!";
    sprintln(F("Error setting up MDNS responder!"));
    while (10) {
      delay(100);
    }
  }

  sprintln("mDNS responder started");
  MDNS.addService("http", "tcp", 80);
}

// json data
// Function to generate JSON strings
String data_JSON() {
    // Create a JSON object, adjust size.
    //StaticJsonDocument<650> doc;
    DynamicJsonDocument doc(650);
    
    // Create "ac" object in the JSON structure
    JsonObject acObj = doc.createNestedObject("ac");
    acObj["input_freq"] = ac.input_freq;
    acObj["input_voltage"] = ac.input_voltage;
    acObj["output_freq"] = ac.output_freq;
    acObj["output_load_percent"] = ac.output_load_percent;
    acObj["output_va"] = ac.output_va;
    acObj["output_voltage"] = ac.output_voltage;
    acObj["output_watts"] = ac.output_watts;

    // Create "dc" object in the JSON structure
    JsonObject dcObj = doc.createNestedObject("dc");
    dcObj["batt_v_compensation_k"] = dc.batt_v_compensation_k;
    dcObj["charge_current"] = dc.charge_current;
    dcObj["charge_power"] = dc.charge_power;
    dcObj["discharge_current"] = dc.discharge_current;
    dcObj["discharge_power"] = dc.discharge_power;
    dcObj["new_k"] = dc.new_k;
    dcObj["pv_current"] = dc.pv_current;
    dcObj["pv_voltage"] = dc.pv_voltage;
    dcObj["pv_power"] = dc.pv_power;
    dcObj["voltage"] = dc.voltage;
    dcObj["voltage_corrected"] = dc.voltage_corrected;

    // Create "inverter" object in the JSON structure
    JsonObject iObj = doc.createNestedObject("inverter");
    iObj["read_interval_ms"] = READ_INTERVAL;
    iObj["valid_info"] = inverter.valid_info;
    iObj["charger"] = inverter.charger;
    iObj["charger_source_priority"] = inverter.charger_source_priority;
    iObj["eff_va"] = inverter.eff_va;
    iObj["eff_w"] = inverter.eff_w;
    iObj["op_mode"] = inverter.op_mode;
    iObj["op_mode"] = inverter.op_mode;
    iObj["output_source_priority"] = inverter.output_source_priority;
    iObj["soc"] = inverter.soc;
    iObj["temp"] = inverter.temp;

    // Serialize the JSON object to a string
    String output;
    serializeJson(doc, output);
    doc.garbageCollect();
    doc.clear();

    // return the json string
    return output;
}

// 404 handler >  redir to /
void notFound(AsyncWebServerRequest *request) {
  request->redirect("/");
}

// index
void serveIndex(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html");
  #ifdef VERBOSE_SERIAL
    sprintln("/ ");
  #endif
}

//  status
void serveStatus(AsyncWebServerRequest *request) {
  request->send(200, "application/json", data_JSON());
  #ifdef VERBOSE_SERIAL
    sprintln("/status");
  #endif
}

void webserver_setup() {
  // defaults

  // not found
  server.onNotFound(notFound);

  // Route for root / web page
  server.on("/", HTTP_GET, serveIndex);

  // staus in json format
  server.on("/api/status", HTTP_GET, serveStatus);

  #ifdef WEBSERIAL
    // WebSerial is accessible at "<IP Address>/webserial" in browser
    WebSerial.begin(&server);
    
    /* Attach Message Callback */
    WebSerial.onMessage([&](uint8_t *data, size_t len) {
      Serial.printf("Received %u bytes from WebSerial: ", len);
      Serial.write(data, len);
      Serial.println();
      WebSerial.println("Received Data...");
      String d = "";
      for(size_t i=0; i < len; i++){
        d += char(data[i]);
      }
      WebSerial.println(d);
    });

    // print
    sprintln("WebSerial Setup");
  #endif

  // start server
  server.begin();
}

void do_wifi() {
  // set wifi power level
  WiFi.setTxPower(WIFI_POWER_5dBm);

  // try to connect in client mode
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.begin(c_ssid, c_password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    // no connect
    sprintln("No Wifi Net, back to AP mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_AP);
    delay(50);

    // start AP
    WiFi.softAP(s_ssid, s_password);
    wifi_mode = 1;

    myIP = WiFi.softAPIP();
  } else {
    // connected to a client netwok
    myIP = WiFi.localIP();
    sprintln("Connected to existent Wifi");
    wifi_mode = 0;
  }

  // Wifi notice
  sprintln("WiFi Ready");
  sprint("IP address: ");
  sprintln(myIP);
}

void check_wifi() {
  // check if connected
  if (WiFi.status() != WL_CONNECTED & wifi_mode == 0) {
    do_wifi();
  }
}

void wifi_scan() {
  sprintln("Start wifi Scan for the AP");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    sprintln("no networks found");
  } else {
    // search for our net
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == c_ssid) {
        sprintln("AP found, try to conenct");
        do_wifi();
      } else {
        sprintln("No networks found");
      }
    }
  }
}

#ifdef PUBSUB
  void psclient_callback(char* topic, byte* payload, unsigned int length) {
    sprint("MQTT Message arrived [");
    sprint(topic);
    sprint("] ");
    for (int i = 0; i < length; i++) {
      sprint((char)payload[i]);
    }
    sprintln();
  }

void psclient_reconnect() {
  // Loop until we're reconnected
  if (!psclient.connected()) {
    sprint("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32_";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (psclient.connect(clientId.c_str())) {
      sprintln("connected");
      psclient.subscribe(subscribeTopic);
    } else {
      sprint("failed, rc=");
      sprint(psclient.state());
      sprintln(" ");

      // should we retry ???
      // Wait 5 seconds before retrying
      //delay(5000);
      //timerId = timer.setTimeout(5000,reconnect);
    }
  }
}

// PubSub dummy...
void pubsub_dummy() {
  static uint8_t i = 0;

  // increment every time
  i++;
  
  publish(subscribeTopic, i);

  // fold back
  if (i == 255) {i = 0;}
}
#endif

void idle() {
    // do nothing
    timer.run();
    yield();
}

void node_setup() {
  // communicate with Modbus slave ID 5 over Serial
  node.begin(5, SSerial);
  node.idle(idle);
  // private method
  //node.ku16MBResponseTimeout = 20; // ms of response timeout

  // timer.setInterval(5000, alivePrint);
  timer.setInterval(READ_INTERVAL, send_request);
}

void setup() {
  // normal serial via USB and gpios txd0/rxd0
  Serial.begin (115200);

  // Wifi connect, client and start server if not client
  do_wifi();

  #ifdef PUBSUB
    // mosquitto pubsub setup
    psclient.setServer(mqtt_server, mqtt_server_port);
    psclient.setCallback(psclient_callback);

    // timer functions related to pubsub
    timer.setInterval(60000, pubsub_dummy);
    timer.setInterval(5000, psclient_reconnect);
  #endif

  // webserver start
  webserver_setup();

  // delay to allow reconnection
  delay(5*1000);

  //  OTA
  OTA_setup();
  ArduinoOTA.begin();

  // notice abou OTA
  sprintln("OTA ready");

  // Set up mDNS responder:
  mDNS_setup();
  
  // PowMr software serial
  SSerial.begin(2400, SWSERIAL_8N1, RXD2, TXD2, false);
  if (SSerial) {
    sprintln("SSerial init ok");
  } else {
    sprintln("SSerial init problem !!!");
  }

  // notice to the user
  sprint("Firmware version: ");
  sprintln(VERSION);

  // SPIFFS
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    sprintln("SPIFFS Mount Failed");
  } else {
    sprintln("SPIFFS init OK");
  }

  // modbus setup
  node_setup();

  // init ended
  sprintln("Ready to rock...");
}

void loop() {
  // check wifi
  check_wifi();

  // handle OTA uppdates
  ArduinoOTA.handle();

  // timer
  timer.run();

  #ifdef PUBSUB
    // pubsub thinfs
    psclient.loop();
  #endif

  // delay
  delay(1);
}

