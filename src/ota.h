// OTA updates header
// Modularized from main.cpp

#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// Initialize OTA
void otaSetup();

// Initialize mDNS
void mdnsSetup();

// Restart OTA and mDNS after the WiFi stack has been reset
void networkServicesRestart();

#endif // OTA_H
