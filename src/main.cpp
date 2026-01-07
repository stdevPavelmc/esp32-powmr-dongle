
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
// #include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SimpleTimer.h>          // Simple Task Time Manager

#include "wifi.h"

#define WEBSERIAL 1
#define VERSION             1.04

// rewrite prints
#ifdef WEBSERIAL
  #define sprint(x) WebSerial.print(x)
  #define sprintln(x) WebSerial.println(x)
#else
  #define sprint(x) Serial.print(x)
  #define sprintln(x) Serial.println(x)
#endif

#define DEBUG(x)  sprintln(x);

// simple timer
SimpleTimer timer;

// modbus data
ModbusMaster node;

// webserver related
AsyncWebServer server(80);
IPAddress myIP;

// SPIFFS
#define FORMAT_SPIFFS_IF_FAILED true

// Wifi status
bool wifi_mode = 0; // 0 = client | 1 = AP

// define serial connection got SSerial
#define TXD2   GPIO_NUM_17  // TXD2
#define RXD2   GPIO_NUM_16  // RXD2

// define SoftWare Serial
EspSoftwareSerial::UART SSerial;

//flag for saving data
bool shouldSaveConfig = false;

// MQTT
char conf_server_ip[32] = "mqtt.local";
char conf_server_port[6] = "1883";
uint16_t conf_server_port_int;

// global invertor state

uint8_t i_state_available = 0;
float batt_v_compensation_k = 0.01;  // about 0.4v at 60a, 60 * x = 0.4 
float batt_v_corrected; 

uint8_t charger_active = 0;

#define READ_INTERVAL  15000 // 15 seconds, it's millis

#define BATT_MAX_VOLTAGE 28.8
#define BATT_MIN_VOLTAGE 23.0

uint16_t op_mode;
float ac_voltage;
float ac_freq;
float pv_voltage;
float pv_power;
float batt_voltage;
float batt_voltage_;
float batt_charge_current;
float batt_charge_current_;
float batt_discharge_current;
float batt_discharge_current_;
float output_va;
float output_watts;
float output_voltage;
float output_freq;
float load_percent;

float batt_soc = 0;
float inverter_temp = 0;

// battery upper float voltage setpoint
float batt_voltage_charged = 27.2;
uint8_t batt_max_charge_current = 60;
uint8_t charge_current = 0;

#define TIME_OUT  15
// 512
#define MAX_MBUS_PKT  500

void set_util_charge_current(uint8_t); 

/**
 * @fn int strend(const char *s, const char *t)
 * @brief Searches the end of string s for string t
 * @param s the string to be searched
 * @param t the substring to locate at the end of string s
 * @return one if the string t occurs at the end of the string s, and zero otherwise
 */
int strend(const char *s, const char *t) {
    size_t ls = strlen(s); // find length of s
    size_t lt = strlen(t); // find length of t
    if (ls >= lt)  // check if t can fit in s
    {
        // point s to where t should start and compare the strings from there
        return (0 == memcmp(t, s + (ls - lt), lt));
    }
    return 0; // t was longer than s
}

void alivePrint() {
  Serial.print(".");
}

void hexDump(const uint8_t*b, int len){
  //#ifdef DEBUG_PRINTS
  Serial.println();
  for (int i=0; i < len; i = i + 16) {
    Serial.print("           ");
    for(int x=0; x<16 && (x+i) < len; x++) {
      if(b[i+x]<=0xf) Serial.print("0");
      Serial.print(b[i+x],HEX);
      Serial.print(" ");
    }
    Serial.print(" ");
    for(int x=0; x<16 && (x+i) < len; x++) {
      if (b[i+x]<=32||b[i+x] >= 126) {
          Serial.print(".");
      } else Serial.print((char)b[i+x]);
    }
    Serial.print("\n");
  }
  Serial.print("                   Length: ");
  Serial.println(len);
//  #endif
}

void falltosleep() {
  DEBUG("Sleep...\n");
  ESP.deepSleep(60e6); // 60 sec
  // RF_NO_CAL
  // ESP.deepSleepInstant(microseconds, mode); // mode WAKE_RF_DEFAULT, WAKE_RFCAL, WAKE_NO_RFCAL, WAKE_RF_DISABLED
}

// // mqtt subscribe callback / command topic
// void callback(char* topic, byte* payload, unsigned int length) {
//   Serial.print("\nMessage arrived [");
//   Serial.print(topic);
//   Serial.print("] ");
//   for (int i = 0; i < length; i++) {
//     Serial.print((char)payload[i]);
//   }
//   Serial.println();

//   if (strend(topic,"/txpow")) {
//       Serial.println("TX power set");
//       payload[length] = '\0'; // might be unsafe
//       float txpow = atof((char *)payload);
//       WiFi.setOutputPower(txpow);        // float 0 - 20.5 ->>> 4.0 * val (0-82)
//   }

//   if (strend(topic,"/reboot")) {
//     Serial.println("Topic Reboot...");
//     Serial.flush();
//     delay(1000);
//     ESP.reset();
//     delay(5000);
//   }

//   #define TOKEN_SEP   " "

//   // set util charge current to 2 a
//   //  '/iot/node/powmr/c/modbus_write_single' -m "5024 2"
//   //  
//   // set solar only charger: 
//   // func 6, (5017), 0003
//   // /iot/node/powmr/s/rec 0506139900031ce4
//   //
//   // set util and solar charger:
//   // func 6, (5017), 0002

//   if (strend(topic,"/modbus_write_single")) {
//     uint16_t reg_n; 
//     uint16_t reg_val;
//     char* ptr;
//     ptr = (char *)payload;
//     static char* seq_tok_last; // last char in tokenizer

//     *(payload + length) = '\0';
//     ptr = strtok_r(ptr, TOKEN_SEP, &seq_tok_last);

//     if (ptr != NULL) {
//       reg_n = atoi(ptr);
//       ptr = strtok_r(NULL, TOKEN_SEP, &seq_tok_last);
//       if (ptr != NULL) {
//         reg_val = atoi(ptr);
//         uint8_t res = node.writeSingleRegister(reg_n, reg_val);
//         char buf[10];
//         snprintf(buf, 9,  "%u", res);
//         client.publish(publishTopicLog, buf);
//       }
//     } 
//   }


//   if (strend(topic,"/modbus_read_single")) {
//     *(payload + length) = '\0';
//     uint16_t reg_n = atoi((char *)payload);

//     uint8_t res = node.readHoldingRegisters(reg_n, 1);
//     uint16_t response;
//     if (res == node.ku8MBSuccess) {
//       response = node.getResponseBuffer(0);
//       char buf[10];
//       char topic_buf[50];
//       snprintf(buf, 9, "%u", response);
//       snprintf(topic_buf, 49, "%s/%u", publishTopic, reg_n);
//       client.publish(topic_buf, buf);
//     } else {
//       client.publish(publishTopicLog, "error reading from holding register");
//     }
//   }

//   if (strend(topic,"/set_charge_current")) {
//     *(payload + length) = '\0';
//     uint16_t cc = atoi((char *)payload);
//     set_util_charge_current(cc);
//   }

//   if (strend(topic,"/set_charge_current_limit")) {
//     *(payload + length) = '\0';
//     batt_max_charge_current = atoi((char *)payload);
//   }

//   if (strend(topic,"/set_batt_voltage_charged")) {
//     *(payload + length) = '\0';
//     batt_voltage_charged = atof((char *)payload);
//   }
// }

// void reconnect() {
//   // Loop until we're reconnected
//   if (!client.connected()) {
//     Serial.print("Attempting MQTT connection...");
//     // Create a random client ID
//     String clientId = "ESP8266Client-";
//     clientId += String(random(0xffff), HEX);
//     // Attempt to connect
//     if (client.connect(clientId.c_str())) {
//       Serial.println("connected");
//       client.subscribe(mqtt_topic_cmd);
//     } else {
//       Serial.print("failed, rc=");
//       Serial.print(client.state());
//       Serial.println(" ");
//       // should we retry ???
//       // Wait 5 seconds before retrying
//       //delay(5000);
//       //timerId = timer.setTimeout(5000,reconnect);
//     }
//   }
// }

int getRSSI() {
  // print the received signal strength:
  int rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI): ");
  Serial.print(rssi);
  Serial.println(" dBm");
  return(rssi);
}

// void publish_float(const char *topic, float var) {
//     char buf[11];
//     snprintf(buf, 10,"%3.1f", var);
//     client.publish(topic, buf);
// }

// void publish_float4(const char *topic, float var) {
//     char buf[11];
//     snprintf(buf, 10,"%3.4f", var);
//     client.publish(topic, buf);
// }


// requests data from Slave device
void send_request() {
  // see register listing here: https://github.com/odya/esphome-powmr-hybrid-inverter/blob/main/docs/registers-map.md
  
  uint8_t j, result;
  #define MAX_MBUS_WORDS  50
  uint16_t data[MAX_MBUS_WORDS];

  #define MAX_MBUS_PKT_S (MAX_MBUS_WORDS*2)

  static uint8_t charArr[2*MAX_MBUS_PKT_S + 1]; //Note there needs to be 1 extra space for this to work as snprintf null terminates.
  uint8_t *myPtr;
  myPtr = charArr; 

      // variables
  // read 45 registers (func 3) from slave 5 starting from address 4501 (Decimal) 
  // 05031195002d9143
      // state
  // read 16 registers (func 3) from slave 5 starting from address 4546 (Decimal) 11C2
  // 050311c20010e142

  uint16_t nregisters = 45;

  sprintln("Sending request... register 4501");
  result = node.readHoldingRegisters(4501, nregisters);

  // do something with data if read is successful
  if (result == node.ku8MBSuccess)
  {
    for (j = 0; j < 45; j++)
    {
      data[j] = node.getResponseBuffer(j);
    }

    // dump data to mqtt topic
    // myPtr = charArr; 
    // for (uint16_t i = 0; i < nregisters; i++){
    //   snprintf((char *)myPtr, 5,"%04x", data[i]); 
    //   myPtr += 4; 
    // }

    // client.publish(publishTopic_raw1, charArr, nregisters*2*2);

    // convert and publish variables

    /* op_mode
      0: 
      1: 
      2: 
      3: Battery Only
      4: AC
    */

    op_mode = htons(data[0]); // 4501
    myPtr = charArr; 
    snprintf((char *)myPtr, 5, "%04x", op_mode);
    // client.publish("/iot/node/powmr/s/mode", charArr, strlen((char *)charArr));
    sprint("op_mode: ");
    sprintln(op_mode);

    ac_voltage = htons(data[1]) / 10.0; // 4502
    // publish_float("/iot/node/powmr/s/ac_voltage", ac_voltage);
    sprint("ac_voltage: ");
    sprintln(ac_voltage);

    ac_freq = htons(data[2]) / 10.0; // 4503
    // publish_float("/iot/node/powmr/s/ac_freq", ac_freq);
    sprint("ac_freq: ");
    sprintln(ac_freq);

    pv_voltage = htons(data[3]) / 10.0; // 4504
    // publish_float("/iot/node/powmr/s/pv_voltage", pv_voltage);
    sprint("pv_voltage: ");
    sprintln(pv_voltage);

    pv_power = (float) htons(data[4]);  // 4505
    // publish_float("/iot/node/powmr/s/pv_power", pv_power);
    sprint("pv_power: ");
    sprintln(pv_power);

    batt_voltage = htons(data[5]) / 10.0; // 4506
    // publish_float("/iot/node/powmr/s/batt_voltage", batt_voltage);
    sprint("batt_voltage: ");
    sprintln(batt_voltage);

    // 4507: SoC pero muy innexacto si no habla con el battery pack

    batt_charge_current = htons(data[7]); // 4508
    // publish_float("/iot/node/powmr/s/batt_charge_current", batt_charge_current);
    sprint("batt_charge_current: ");
    sprintln(batt_charge_current);

    batt_discharge_current = htons(data[8]); // 4509
    // publish_float("/iot/node/powmr/s/batt_discharge_current", batt_discharge_current);
    sprint("batt_discharge_current: ");
    sprintln(batt_discharge_current);

    output_voltage = htons(data[9]) / 10.0; // 4510
    // publish_float("/iot/node/powmr/s/output_voltage", output_voltage);
    sprint("output_voltage: ");
    sprintln(output_voltage);

    output_freq = htons(data[10]) / 10.0; // 4511
    // publish_float("/iot/node/powmr/s/output_freq", output_freq);
    sprint("output_freq: ");
    sprintln(output_freq);

    output_va = htons(data[11]); // 4512
    // publish_float("/iot/node/powmr/s/output_va", output_va);
    sprint("output_va: ");
    sprintln(output_va);

    output_watts = htons(data[12]); // [/ 20.0]?; // 4513
    // publish_float("/iot/node/powmr/s/output_watts", output_watts);
    sprint("output_watts: ");
    sprintln(output_watts);

    load_percent = htons(data[13]); // [/ 20.0]?; // 4514
    // publish_float("/iot/node/powmr/s/load_percent", load_percent);
    sprint("load_percent: ");
    sprintln(load_percent);

    // update wires resistanse coeff
    float new_k;
    //uint8_t update_k = 0;
    /*
    if (i_state_available && batt_discharge_current_ - batt_discharge_current > 10) {
      new_k = abs(batt_voltage_ - batt_voltage) / abs(batt_discharge_current_ - batt_discharge_current);
      update_k = 1;
    }
    if (i_state_available && batt_charge_current - batt_charge_current_ > 10) {
      new_k = abs(batt_voltage - batt_voltage_) / abs(batt_charge_current - batt_discharge_current_);
      update_k = 1;
    }
    */
    float charge_current_change = -(batt_discharge_current - batt_discharge_current_) 
                                    + (batt_charge_current - batt_charge_current_);
    if (i_state_available 
          &&  abs(charge_current_change) > 5.0) {
        new_k = (batt_voltage - batt_voltage_) / charge_current_change;

        batt_v_compensation_k = batt_v_compensation_k + (new_k - batt_v_compensation_k) * 0.1;

        myPtr = charArr; 
        snprintf((char *)myPtr, 100,"New voltage coeff: %3.5f, updated batt_v_compensation_k %3.5f", new_k, batt_v_compensation_k); 
        // client.publish(publishTopicLog, (char *) charArr);
        // sprintln(myPtr);
    }

    batt_voltage_ = batt_voltage;
    batt_charge_current_ = batt_charge_current;
    batt_discharge_current_ = batt_discharge_current;

    i_state_available = 1;
  }
  else
  {
    sprintln("node read error registers 4501");
    i_state_available = 0;
  }

  nregisters = 16;
  sprintln("Sending request... register 4546");
  result = node.readHoldingRegisters(4546, nregisters);

  if (result == node.ku8MBSuccess)
  {
    for (j = 0; j < 45; j++)
    {
      data[j] = node.getResponseBuffer(j);
    }
    // // dump data to mqtt topic

    // myPtr = charArr; 

    // for (uint16_t i = 0; i < nregisters; i++){
    //   snprintf((char *)myPtr, 5,"%04x", data[i]); 
    //   myPtr += 4;
    // }

    // client.publish(publishTopic_raw2, charArr, nregisters*2*2);

    inverter_temp = htons(data[11]);
    // publish_float("/iot/node/powmr/s/inverter_temp", inverter_temp);
    sprint("inverter_temp: ");
    sprintln(inverter_temp);
  }
  else
  {
    sprintln("node read error registers 4546");
  }
}

// runs when we are waiting for modbus data
void idle() {
    ArduinoOTA.handle();

    yield();
}

uint8_t allowed_charger_currents[] = {0, 2, 10, 20, 30, 40, 50, 60};

// set utility charge current
// 0 - switch to solar only
// 2, 10, 20, 30, 40, 50, 60
void set_util_charge_current(uint8_t cc) {
  uint8_t res;
  uint8_t try_cnt = 0;
  char buf[60];

    if (cc == 0) {
      charger_active = 0;
      while ( // solar only
              (res = node.writeSingleRegister(5017, 3)) != 0 
              && try_cnt++ < 5
      );
      if (res != 0) { charger_active = 1; }
      snprintf(buf, 50,  "Charger disconnected, solar only, res = %u", res);
      // client.publish(publishTopicLog, buf);
      sprintln(buf);
    } else {
      // set utility charge current
      while ( 
              (res = node.writeSingleRegister(5024, cc)) != 0 
              && try_cnt++ < 5 
      );
      snprintf(buf, 50,  "Charger current set to %u A, res = %u", cc, res);
      // client.publish(publishTopicLog, buf);
      sprintln(buf);

      if (!charger_active) {
        charger_active = 1;
        try_cnt = 0;
        // set util and solar charge mode
        while (
                (res = node.writeSingleRegister(5017, 2)) != 0
                && try_cnt++ < 5
        );
        if (res != 0) { charger_active = 0; }
        snprintf(buf, 50,  "Charger enabled, res = %u", res);
        // client.publish(publishTopicLog, buf);
        sprintln(buf);
      } 
    }
}

// runs periodically to monitor and control battery voltage 
void controller() {
  if (i_state_available) {
    batt_v_corrected = batt_voltage - (batt_v_compensation_k * batt_charge_current)
                                    + (batt_v_compensation_k * batt_discharge_current);
    // publish_float4("/iot/node/powmr/s/batt_voltage_corrected", batt_v_corrected);
    // publish_float4("/iot/node/powmr/s/batt_v_compensation_k", batt_v_compensation_k);
    sprint("batt_v_corrected: ");
    sprintln(batt_v_corrected);
    sprint("batt_v_compensation_k: ");
    sprintln(batt_v_compensation_k);
    
    batt_soc = 100.0 * (batt_voltage - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE);
    // publish_float("/iot/node/powmr/s/batt_soc", batt_soc);
    sprint("batt_soc: ");
    sprintln(batt_soc);

/*
    uint16_t charge_current = 2;
    if (batt_voltage_charged - batt_v_corrected > 0.4) {
      // set max allowed charge current
      charge_current = batt_max_charge_current;
    } else if (batt_voltage_charged - batt_v_corrected > 0.2) {
      charge_current = batt_max_charge_current / 2;
      if (charge_current < 10) charge_current = 2;
      if (charge_current == 25) charge_current = 20;
      if (charge_current == 15) charge_current = 10;
    } else if (batt_voltage_charged - batt_v_corrected >= 0.1) {
      charge_current = 2;
    } else if (batt_voltage_charged <= batt_v_corrected) {
      charge_current = 0;
    } 
    set_util_charge_current(charge_current);

    if (batt_voltage == batt_voltage_charged) {
      // do nothing, keep the same charge current
    } else {
      // battery voltage is out of desired state
      
      if (batt_voltage >= batt_voltage_charged) {
        // decrease charger current
        if (charge_current == 2) {
          charge_current = 0;
        } else if (charge_current == 10) {
          charge_current = 2;
        } else if (charge_current > 10) {
          charge_current = charge_current - 10;
        }
      } else {
        // increase charger current
        if (charge_current < batt_max_charge_current) {
            if (charge_current == 0) {
              charge_current = 2;
            } else if (charge_current == 2) {
              charge_current = 10;
            } else if (charge_current < 60) {
              charge_current = charge_current + 10;
            }
        }
      }

        // if (batt_voltage_charged - batt_voltage >= 0.3) {
        //   // set max allowed charge current
        //   charge_current = batt_max_charge_current;
        // } else if (batt_voltage_charged - batt_voltage >= 0.2) {
        //   if (batt_max_charge_current < 10) {
        //     charge_current = 2;
        //   } else {
        //     charge_current = 20;
        //   }
        // } else if (batt_voltage_charged - batt_voltage >= 0.1) {
        //   if (charge_current == 0) {
        //     charge_current = 2;
        //   }
        // }
    }

    if (charge_current > batt_max_charge_current) {
      charge_current = batt_max_charge_current;
    }

    set_util_charge_current(charge_current); 
  */
  }
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

// 404 handler >  redir to /
void notFound(AsyncWebServerRequest *request) {
  request->redirect("/");
}

// index
void serveIndex(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html");
  #ifdef DEBUG
    sprintln("/ ");
  #endif
}

void webserver_setup() {
  // defaults

  // Route for root / web page
  server.on("/", HTTP_GET, serveIndex);

  // not found
  server.onNotFound(notFound);

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

void node_setup() {
  // communicate with Modbus slave ID 5 over Serial
  node.begin(5, SSerial);
  node.idle(idle);
  // private method
  //node.ku16MBResponseTimeout = 20; // ms of response timeout

  // timer.setInterval(5000, alivePrint);
  timer.setInterval(READ_INTERVAL, send_request);
  timer.setInterval(READ_INTERVAL *2, controller);
}

void setup() {
  // normal serial via USB and gpios txd0/rxd0
  Serial.begin (115200);

  // Wifi connect, client and start server if not client
  do_wifi();

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

}
