// WiFi management header
// Modularized from main.cpp

// credentials lies in wifi_creds.h

#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

// Connect to WiFi (or start AP if fails)
void doWifi();

// Check WiFi connection and reconnect if needed
void checkWifi();

#endif // WIFI_H
