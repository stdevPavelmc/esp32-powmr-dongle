// OTA updates implementation
// Modularized from main.cpp

#include "ota.h"
#include "globals.h"
#include "energy.h"
#include "wifi_creds.h"
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

// OTA UDP port (espota default)
#define OTA_PORT 3232

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// Service state, used to avoid double initialization and to know whether a
// restart is required after the WiFi stack has been reset.
static bool ota_ready = false;
static bool mdns_ready = false;

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

  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setHostname(hostname);

  // mDNS is handled by mdnsSetup() so the HTTP and OTA services are
  // advertised together, exactly once.
  ArduinoOTA.setMdnsEnabled(false);

  // Bind the OTA UDP socket. This must happen after WiFi is up and again
  // whenever the WiFi stack has been restarted.
  ArduinoOTA.begin();
  ota_ready = true;
  sprint("OTA server started on port ");
  sprintln(OTA_PORT);
}

// Initialize mDNS (idempotent, never blocks setup on failure)
void mdnsSetup() {
  if (mdns_ready) {
    return;
  }

  if (!MDNS.begin(hostname)) {
    sprintln(F("Error setting up MDNS responder!"));
    return;
  }

  MDNS.addService("http", "tcp", 80);
  MDNS.enableArduino(OTA_PORT, false);
  mdns_ready = true;

  sprint("mDNS responder started: ");
  sprint(hostname);
  sprintln(".local");
}

// Restart OTA and mDNS after a WiFi reconnect.
// Resetting the WiFi stack can leave the OTA UDP socket and the mDNS responder
// in a stale state, which silently breaks OTA until the next reboot.
void networkServicesRestart() {
  // Services were never started (still booting) - nothing to restore.
  if (!ota_ready && !mdns_ready) {
    return;
  }

  ArduinoOTA.end();
  ota_ready = false;

  if (mdns_ready) {
    MDNS.end();
    mdns_ready = false;
  }

  otaSetup();
  mdnsSetup();
}
