// Main entry point - setup and loop only
// Modularized from main.cpp

// Arduino core
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <AsyncTCP.h>

// Local includes
#include "config.h"
#include "data.h"
#include "globals.h"
#include "utils.h"
#include "modbus.h"
#include "energy.h"
#include "webserver.h"
#include "ota.h"
#include "wifi.h"
#include "wifi_creds.h"

// ==================== GLOBAL VARIABLES ====================

// Preferences for persistent storage
Preferences prefs;

// Modbus node
ModbusMaster node;
uint16_t mbusData[MBUS_REGISTERS + 1];

// Web server
AsyncWebServer server(80);
IPAddress myIp;

// WiFi status: 0 = client, 1 = AP
bool wifiMode = 0;

// Timing variables
unsigned long lastSendRequestTime = 0;
unsigned long lastWifiCheckTime = 0;

// Dynamic read interval
float dynamic_read_interval = INITIAL_READ_INTERVAL;
uint8_t consecutive_failures = 0;

// EWMA tracking variables
bool read_time_initialized = false;
float autonomy_efficiency_ewma = 0.0;
float autonomy_watts_ewma = 0.0;
bool autonomy_initialized = false;

// Data instances
ACData ac;
DCData dc;
InverterData inverter;

// ==================== PRINT MACROS ====================

// Print macros
#ifdef WEBSERIAL
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// ==================== SETUP ====================

void setup() {
  Serial.begin(MONITOR_SERIAL_SPEED);

  doWifi();

  webserverSetup();

  delay(5*1000);

  otaSetup();
  ArduinoOTA.begin();

  sprintln("OTA ready");

  mdnsSetup();

  sprint("Firmware version: ");
  sprintln(VERSION);

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    sprintln("SPIFFS Mount Failed");
  } else {
    sprintln("SPIFFS init OK");
  }

  nodeSetup();

  // Initialize timing variables for manual timer replacement
  lastSendRequestTime = millis();
  lastWifiCheckTime = millis();

  sprintln("Ready to rock...");
}

// ==================== LOOP ====================

void loop() {
  ArduinoOTA.handle();

  // Manual timing checks (replacing SimpleTimer)
  unsigned long currentTime = millis();

  // Check if it's time to call sendRequest
  if (hasTimeElapsed(lastSendRequestTime, currentTime, uint16_t(dynamic_read_interval * 1000))) {
    lastSendRequestTime = currentTime;
    sendRequest();
  }

  // Check if it's time to call checkWifi (every 3 minutes)
  if (hasTimeElapsed(lastWifiCheckTime, currentTime, 3 * 60 * 1000UL)) {
    lastWifiCheckTime = currentTime;
    checkWifi();
  }

  delay(1);
}
