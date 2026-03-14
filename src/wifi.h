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

// Attempt to reconnect to client WiFi when in AP mode (called periodically)
void tryReconnectToClient();

// Initialize WiFi mode from persistent storage
void initWifiMode();

#endif // WIFI_H
