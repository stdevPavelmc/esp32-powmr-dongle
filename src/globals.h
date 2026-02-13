// Global variables - extern declarations
// Modularized from main.cpp

#ifndef GLOBALS_H
#define GLOBALS_H

#include "data.h"
#include "config.h"
#include <Preferences.h>
#include <ModbusMaster.h>
#include <ESPAsyncWebServer.h>
#include <IPAddress.h>

// Print macros - defined here but implemented via includes in .cpp files
// The actual sprint/sprintln macros are defined in main.cpp and other .cpp files

// Preferences for persistent storage
extern Preferences prefs;

// Modbus node
extern ModbusMaster node;
extern uint16_t mbusData[MBUS_REGISTERS + 1];

// Web server
extern AsyncWebServer server;
extern IPAddress myIp;

// WiFi status: 0 = client, 1 = AP
extern bool wifiMode;

// Timing variables
extern unsigned long lastSendRequestTime;
extern unsigned long lastWifiCheckTime;

// Dynamic read interval
extern float dynamic_read_interval;
extern uint8_t consecutive_failures;

// EWMA tracking variables
extern bool read_time_initialized;
extern float autonomy_efficiency_ewma;
extern float autonomy_watts_ewma;
extern bool autonomy_initialized;

// Data instances
extern ACData ac;
extern DCData dc;
extern InverterData inverter;

#endif // GLOBALS_H
