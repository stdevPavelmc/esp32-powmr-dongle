// OTA updates implementation
// Modularized from main.cpp

#include "ota.h"
#include "globals.h"
#include "energy.h"
#include "wifi_creds.h"
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// Initialize OTA updates
void otaSetup() {
  ArduinoOTA
      .onStart([]() {
        // Force save energy data before OTA update
        saveEnergyData(true);
        Serial.println("Energy data force saved before OTA update");

        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else {
          type = "filesystem";
        }

        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
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
        }
      });

  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(hostname);
}

// Initialize mDNS
void mdnsSetup() {
  if (!MDNS.begin(hostname)) {
    sprintln(F("Error setting up MDNS responder!"));
    while (10) {
      delay(100);
    }
  }

  sprintln("mDNS responder started");
  MDNS.addService("http", "tcp", 80);
}
