// All configuration defines, constants, and thresholds
// Modularized from main.cpp

#ifndef CONFIG_H
#define CONFIG_H

/********* Configurable flags *************/
#define WEBSERIAL 1
#define VERBOSE_SERIAL 1
// #define DEBUG_AC 1
// #define DEBUG_DC 1
// #define DEBUG_INVERTER 1

#define MONITOR_SERIAL_SPEED 9600
#define VERSION  3.0

// Preferences save thresholds
#define SAVE_THRESHOLD_PV 5.0       // 5 Wh
#define SAVE_THRESHOLD_BATT 5.0     // 1 Wh
#define SAVE_THRESHOLD_GG 5.0       // 1%
#define SAVE_THRESHOLD_AC 20.0      // 50 Wh

// Battery Gas Gauge Configuration
const float MAXIMUM_ENERGY = 12.8*100*2;  // Wh
const float MINIMUM_VOLTAGE = 22.0;   // V
const float MAXIMUM_VOLTAGE = 28.8;   // V

// Autonomy Calculation Configuration
#define AUTONOMY_MAX_DAYS 2              // Maximum autonomy cap in days
#define AUTONOMY_EFFICIENCY_CAP 93.0     // Maximum efficiency cap (%)
#define AUTONOMY_WINDOW_MINUTES 5.0      // Time window for averaging (minutes)

// Modbus configuration
#define MBUS_REGISTERS 61 // Words uint16, starting from 4501 to 4562
#define CHUNK_SIZE 3
#define RETRY_COUNT 4
#define CHUNK_DELAY_US 10

// Dynamic read interval
#define INITIAL_READ_INTERVAL 5.0 // 5 seconds initial

// Serial pins for Modbus
#define TXD2   GPIO_NUM_17  // TXD2
#define RXD2   GPIO_NUM_16  // RXD2

// SPIFFS
#define FORMAT_SPIFFS_IF_FAILED true

// Battery voltage range
#define BATT_MAX_VOLTAGE 28.8
#define BATT_MIN_VOLTAGE 23.0

// Consecutive failures threshold
const uint8_t MAX_FAILURES = 3;

#endif // CONFIG_H
