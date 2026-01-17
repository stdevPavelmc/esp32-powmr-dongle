
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
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <SimpleTimer.h>
#include <Preferences.h>

// local config on files
#include "wifi.h"

/********* Configurable flags *************/
#define WEBSERIAL 1
#define VERBOSE_SERIAL 1
#define MONITOR_SERIAL_SPEED 9600
#define VERSION  3.0

// Preferences save thresholds
#define SAVE_THRESHOLD_PV 0.05      // 50 Wh
#define SAVE_THRESHOLD_BATT 1.0     // 1 Wh
#define SAVE_THRESHOLD_GG 1.0       // 1%
#define SAVE_THRESHOLD_AC 0.05      // 50 Wh

// Battery Gas Gauge Configuration
const float MAXIMUM_ENERGY = 12.8*100*2;  // Wh
const float MINIMUM_VOLTAGE = 22.0;   // V
const float MAXIMUM_VOLTAGE = 28.8;   // V

// Tracking variables
unsigned long lastUpdateMillis = 0;
bool firstRun = true;

// rewrite prints
#ifdef WEBSERIAL
  #define sprint(x) WebSerial.print(x)
  #define sprintln(x) WebSerial.println(x)
#else
  #define sprint(x) Serial.print(x)
  #define sprintln(x) Serial.println(x)
#endif

// simple timer
SimpleTimer timer;

// preferences for persistent storage
Preferences prefs;

// modbus data
ModbusMaster node;
#define MBUS_REGISTERS 61 // Words uint16, starting from 4501 to 4562
#define CHUNK_SIZE 3
#define RETRY_COUNT 4
#define CHUNK_DELAY_US 10
uint16_t mbusData[MBUS_REGISTERS + 1]; // allow for room

// webserver related
AsyncWebServer server(80);
IPAddress myIp;

// SPIFFS
#define FORMAT_SPIFFS_IF_FAILED true

// Wifi status
bool wifiMode = 0; // 0 = client | 1 = AP

// define serial connection got SSerial
#define TXD2   GPIO_NUM_17  // TXD2
#define RXD2   GPIO_NUM_16  // RXD2

// Add these global variables near the top with other globals
#define INITIAL_READ_INTERVAL 15000 // 15 seconds initial
uint16_t dynamic_read_interval = INITIAL_READ_INTERVAL;
uint8_t consecutive_failures = 0;
const uint8_t MAX_FAILURES = 3;

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
  float power_factor;
};

ACData ac;

// dc data
struct DCData {
  float pv_voltage;
  float pv_power;
  float pv_current;
  float pv_energy_produced;
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
  /* Mapping
    0: b0000: ?
    1: b0001: ? AC cargando ?
    2: b0010: ?
    3: b0011: Descargando
    4: b0100: AC cargando
  */
  uint16_t charger;
  /* Mapping in progress
    10: b1010: Descargando desde Batería, no AC, no PV _chargers_off_?
    11: b1011: AC off, PV on (charging from PV) charge in progress _MPPT_ACTIVE ?
    12: b1100: _MPPT_and_AC_ACTIVE_ ?
      AC on, PV on (charging form both) charge in progress
      AC on, PV off (charging form AC) charge in progress
    13: b1101: AC on, PV on, completed charge, load from PV, _charger IDLE_ ?
  */
  float eff_w = 0;
  float soc = 0;    // 0-100% per voltage range
  byte valid_info = 0;
  unsigned int read_time = 0;
  unsigned int read_time_mean = 0;
  float battery_energy = 0.0;
  float gas_gauge = 0.0;
  float energy_spent_ac = 0.0;
  float energy_source_ac = 0.0;
  float energy_source_batt = 0.0;
  float energy_source_pv = 0.0;
};

InverterData inverter;

String uptime() {
  unsigned long uptime_sec = millis() / 1000;
  unsigned long days = uptime_sec / (60 * 60 * 24);
  unsigned long hours = (uptime_sec % (60 * 60 * 24)) / (60 * 60);
  unsigned long minutes = (uptime_sec % (60 * 60)) / 60;
  
  String uptime_str;
  if (days > 0) {
    uptime_str = String(days) + "d:" + String(hours) + "h:" + String(minutes) + "m";
  } else {
    uptime_str = String(hours) + "h:" + String(minutes) + "m";
  }
  return uptime_str;
}

// dynamic reading interval
uint16_t calculateNextInterval() {
  if (inverter.read_time_mean == 0) {
    return INITIAL_READ_INTERVAL;
  }
  
  // Round up to next 5 second multiple
  uint16_t base = (inverter.read_time_mean / 5000) * 5000;
  if (inverter.read_time_mean % 5000 != 0) {
    base += 5000;
  }
  
  // Ensure minimum of 5 seconds max of 30 seconds
  if (base < 5000) {
    base = 5000;
  }
  if (base > 30000) {
    base = 30000;
  }
  
  return base;
}

// Generic energy accumulation function
void updateEnergy(float &energy, float power, unsigned long &lastMillis, bool &firstCall) {
  unsigned long currentMillis = millis();
  unsigned long deltaMillis;

  if (currentMillis < lastMillis) {
    deltaMillis = (0xFFFFFFFF - lastMillis) + currentMillis + 1;
  } else {
    deltaMillis = currentMillis - lastMillis;
  }

  if (firstCall) {
    firstCall = false;
    lastMillis = currentMillis;
    return;
  }

  float deltaHours = deltaMillis / 3600000.0;
  float energyDelta = power * deltaHours;
  energy += energyDelta;

  if (energy < 0.0) {
    energy = 0.0;
  }

  lastMillis = currentMillis;
}

void updateBatteryEnergy(float voltage, float chargeCurrent, float dischargeCurrent) {
  static bool firstRun = 1;
  static unsigned long lastUpdateMillis = 0;
  
  if (voltage <= MINIMUM_VOLTAGE) {
    inverter.battery_energy = 0.0;
    inverter.gas_gauge = 0.0;
    sprintln("Battery depleted - Reset to 0%");
    return;
  }

  if (voltage >= MAXIMUM_VOLTAGE) {
    inverter.battery_energy = MAXIMUM_ENERGY;
    inverter.gas_gauge = 100.0;
    sprintln("Battery full - Reset to 100%");
    return;
  }

  float netCurrent = chargeCurrent - dischargeCurrent;
  updateEnergy(inverter.battery_energy, (voltage * netCurrent), lastUpdateMillis, firstRun);
  
  // if (inverter.battery_energy < 0.0) {
  //   inverter.battery_energy = 0.0;
  // }
  // if (inverter.battery_energy > MAXIMUM_ENERGY) {
  //   inverter.battery_energy = MAXIMUM_ENERGY;
  // }
  
  if (inverter.battery_energy > 0) {
    inverter.gas_gauge = (inverter.battery_energy * 100) / MAXIMUM_ENERGY;
    
    if (inverter.gas_gauge < 0.0) inverter.gas_gauge = 0.0;
    if (inverter.gas_gauge > 100.0) inverter.gas_gauge = 100.0;
  }
}

// pv energy produced
void updatePVEnergy(float pvVoltage, float pvCurrent, float pvPower) {
  static unsigned long lastPvMillis = 0;
  static bool firstPvCall = true;
  static unsigned long nightStartMillis = 0;
  static bool isNight = false;
  static bool nightResetDone = false;

  unsigned long currentMillis = millis();
  
  if (pvVoltage <= 30) {
    if (!isNight) {
      isNight = true;
      nightStartMillis = currentMillis;
      nightResetDone = false;
    } else {
      unsigned long nightDuration;
      if (currentMillis < nightStartMillis) {
        nightDuration = (0xFFFFFFFF - nightStartMillis) + currentMillis + 1;
      } else {
        nightDuration = currentMillis - nightStartMillis;
      }

      // detect X hours without sun
      if (nightDuration >= (3*3600000) && !nightResetDone) {
        dc.pv_energy_produced = 0.0;
        nightResetDone = true;
        Serial.println("Night detected (6h) - PV energy reset to 0");
      }
    }

    lastPvMillis = currentMillis;
    return;
  } else {
    if (isNight) {
      isNight = false;
      sprint("==> Night END");
    }
  }

  float powerToUse = (pvPower > 0.0) ? pvPower : (pvVoltage * pvCurrent);
  updateEnergy(dc.pv_energy_produced, powerToUse, lastPvMillis, firstPvCall);
}

// Load energy data from Preferences
void loadEnergyData() {
  prefs.begin("energy_data", true); // true = read-only

  if (prefs.isKey("pv_energy")) {
    // Keys exist, load saved values
    dc.pv_energy_produced = prefs.getFloat("pv_energy", 0.0);
    inverter.battery_energy = prefs.getFloat("batt_energy", 0.0);
    inverter.gas_gauge = prefs.getFloat("gas_gauge", 0.0);
    inverter.energy_spent_ac = prefs.getFloat("ac_energy", 0.0);

    sprintln("Loaded energy data from Preferences:");
    sprint("  PV energy: ");
    sprintln(dc.pv_energy_produced);
    sprint("  Battery energy: ");
    sprintln(inverter.battery_energy);
    sprint("  Gas gauge: ");
    sprintln(inverter.gas_gauge);
    sprint("  AC energy spent: ");
    sprintln(inverter.energy_spent_ac);
  } else {
    // First boot - initialize with intelligent defaults
    dc.pv_energy_produced = 0.0;
    inverter.energy_spent_ac = 0.0;

    // Estimate battery energy from current voltage
    if (dc.voltage_corrected >= MINIMUM_VOLTAGE && dc.voltage_corrected <= MAXIMUM_VOLTAGE) {
      float soc = 100.0 * (dc.voltage_corrected - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE);
      soc = constrain(soc, 0, 100);
      inverter.gas_gauge = soc;
      inverter.battery_energy = (soc * MAXIMUM_ENERGY) / 100.0;
    } else {
      inverter.gas_gauge = 0.0;
      inverter.battery_energy = 0.0;
    }

    sprintln("First boot - Initialized energy data with defaults:");
    sprint("  Battery energy (from voltage): ");
    sprintln(inverter.battery_energy);
    sprint("  Gas gauge: ");
    sprintln(inverter.gas_gauge);
  }

  prefs.end();
}

// Save energy data to Preferences when thresholds exceeded
void saveEnergyData(bool force=false) {
  static float last_pv = 0.0;
  static float last_batt = 0.0;
  static float last_gg = 0.0;
  static float last_ac = 0.0;
  static bool first_call = true;

  // On first call, just save the current values as baseline
  if (first_call) {
    last_pv = dc.pv_energy_produced;
    last_batt = inverter.battery_energy;
    last_gg = inverter.gas_gauge;
    last_ac = inverter.energy_spent_ac;
    first_call = false;
    return;
  }

  // Check if any threshold is exceeded
  bool should_save = false;

  if (abs(dc.pv_energy_produced - last_pv) >= SAVE_THRESHOLD_PV) {
    should_save = true;
  }
  if (abs(inverter.battery_energy - last_batt) >= SAVE_THRESHOLD_BATT) {
    should_save = true;
  }
  if (abs(inverter.gas_gauge - last_gg) >= SAVE_THRESHOLD_GG) {
    should_save = true;
  }
  if (abs(inverter.energy_spent_ac - last_ac) >= SAVE_THRESHOLD_AC) {
    should_save = true;
  }

  if (should_save or force) {
    prefs.begin("energy_data", false); // false = read-write

    prefs.putFloat("pv_energy", dc.pv_energy_produced);
    prefs.putFloat("batt_energy", inverter.battery_energy);
    prefs.putFloat("gas_gauge", inverter.gas_gauge);
    prefs.putFloat("ac_energy", inverter.energy_spent_ac);

    prefs.end();

    // Update last saved values
    last_pv = dc.pv_energy_produced;
    last_batt = inverter.battery_energy;
    last_gg = inverter.gas_gauge;
    last_ac = inverter.energy_spent_ac;

    #ifdef VERBOSE_SERIAL
      sprintln("Energy data saved to Preferences");
    #endif
  }
}

// Read registers in chunks with retry logic
uint8_t readRegistersChunked(uint16_t startAddr, uint16_t totalRegs, uint16_t *data) {
  uint16_t chunks = (totalRegs + CHUNK_SIZE - 1) / CHUNK_SIZE;
  uint16_t currentAddr = startAddr;
  uint16_t regsRead = 0;
  
  for (uint16_t chunk = 0; chunk < chunks; chunk++) {
    uint16_t regsToRead = min(CHUNK_SIZE, totalRegs - regsRead);
    uint8_t attempts = 0;
    uint8_t success = 0;

    while (attempts <= RETRY_COUNT && !success) {
      uint8_t result = node.readHoldingRegisters(currentAddr, regsToRead);

      if (result == node.ku8MBSuccess) {
        for (uint16_t j = 0; j < regsToRead; j++) {
          data[regsRead + j] = node.getResponseBuffer(j);
        }
        success = 1;

      } else {
        attempts++;
        if (attempts < RETRY_COUNT) {
          delayMicroseconds(CHUNK_DELAY_US);
        }
      }
    }

    if (!success) {
      sprint("Failed to read chunk at addr ");
      sprint(currentAddr);
      sprint(" after ");
      sprint(attempts);
      sprintln(" attempts");
      return 0;
    }

    regsRead += regsToRead;
    currentAddr += regsToRead;
    
    if (chunk < chunks - 1) {
      delayMicroseconds(CHUNK_DELAY_US);
    }
  }
  
  return 1;
}

void sendRequest() {
  sprintln("Reading registers 4501-4561 (61 regs)");
  unsigned long start = millis();
  if (!readRegistersChunked(4501, MBUS_REGISTERS, mbusData)) {
    sprintln("Error reading registers");
    inverter.valid_info = 0;
    consecutive_failures++;
    
    #ifdef VERBOSE_SERIAL
      sprint("Consecutive failures: ");
      sprintln(consecutive_failures);
    #endif
    
    if (consecutive_failures >= MAX_FAILURES) {
      dynamic_read_interval = INITIAL_READ_INTERVAL;
      consecutive_failures = 0;
      
      #ifdef VERBOSE_SERIAL
        sprintln("Max failures reached - reset to 15s interval");
      #endif
      
      timer.deleteTimer(timer.getNumTimers() - 1);
      timer.setInterval(dynamic_read_interval, sendRequest);
    }
    
    return;
  }

  consecutive_failures = 0;

  unsigned long stop = millis();
  if (stop > start) {
    stop -= start;
    inverter.read_time = (unsigned int)stop;

    byte HIST = 10;
    if (inverter.read_time_mean == 0) {
      inverter.read_time_mean = inverter.read_time;
    } else {
      stop = inverter.read_time_mean;
      inverter.read_time_mean = (float)stop * ((HIST - 1.0)/HIST) + (inverter.read_time / HIST);
    }
    
    #ifdef VERBOSE_SERIAL
      sprint("inverter.read_time: ");
      sprintln(inverter.read_time);
    #endif

    #ifdef VERBOSE_SERIAL
      sprint("inverter.read_time_mean: ");
      sprintln(inverter.read_time_mean);
    #endif
  }

  uint16_t new_interval = calculateNextInterval();
  if (new_interval != dynamic_read_interval) {
    dynamic_read_interval = new_interval;
    
    #ifdef VERBOSE_SERIAL
      sprint("Adjusting read interval to: ");
      sprint(dynamic_read_interval);
      sprintln(" ms");
    #endif
    
    timer.deleteTimer(timer.getNumTimers() - 1);
    timer.setInterval(dynamic_read_interval, sendRequest);
  }
  
  inverter.op_mode = (float)htons(mbusData[0]);
  #ifdef VERBOSE_SERIAL
    sprint("inverter.op_mode: ");
    sprintln(inverter.op_mode);
  #endif

  ac.input_voltage = htons(mbusData[1]) / 10.0;
  #ifdef VERBOSE_SERIAL
    sprint("ac.input_voltage: ");
    sprintln(ac.input_voltage);
  #endif

  ac.input_freq = htons(mbusData[2]) / 10.0;
  #ifdef VERBOSE_SERIAL
    sprint("ac.input_freq: ");
    sprintln(ac.input_freq);
  #endif

  dc.pv_voltage = htons(mbusData[3]) / 10.0;
  if (dc.pv_voltage < 6) {
    dc.pv_voltage = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_voltage: ");
    sprintln(dc.pv_voltage);
  #endif

  dc.pv_power = (float)htons(mbusData[4]);
  if (dc.pv_voltage < 6) {
    dc.pv_power = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_power: ");
    sprintln(dc.pv_power);
  #endif

  if (dc.pv_voltage > 0) {
    dc.pv_current = dc.pv_power / dc.pv_voltage;
  } else {
    dc.pv_current = 0;
  }
  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_current: ");
    sprintln(dc.pv_current);
  #endif

  dc.voltage = htons(mbusData[5]) / 10.0;
  #ifdef VERBOSE_SERIAL
    sprint("dc.voltage: ");
    sprintln(dc.voltage);
  #endif

  dc.charge_current = (float)htons(mbusData[7]);
  #ifdef VERBOSE_SERIAL
    sprint("dc.charge_current: ");
    sprintln(dc.charge_current);
  #endif

  dc.discharge_current = (float)htons(mbusData[8]);
  #ifdef VERBOSE_SERIAL
    sprint("dc.discharge_current: ");
    sprintln(dc.discharge_current);
  #endif

  dc.discharge_power = dc.voltage * dc.discharge_current;
  #ifdef VERBOSE_SERIAL
    sprint("dc.discharge_power: ");
    sprintln(dc.discharge_power);
  #endif

  dc.charge_power = dc.voltage * dc.charge_current;
  #ifdef VERBOSE_SERIAL
    sprint("dc.charge_power: ");
    sprintln(dc.charge_power);
  #endif

  ac.output_voltage = htons(mbusData[9]) / 10.0;
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_voltage: ");
    sprintln(ac.output_voltage);
  #endif

  ac.output_freq = htons(mbusData[10]) / 10.0;
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_freq: ");
    sprintln(ac.output_freq);
  #endif

  ac.output_va = (float)htons(mbusData[11]);
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_va: ");
    sprintln(ac.output_va);
  #endif

  ac.output_watts = (float)htons(mbusData[12]);
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_watts: ");
    sprintln(ac.output_watts);
  #endif

  // power_factor calculated
  if (ac.output_watts > 0 & ac.output_va > 0) {
    ac.power_factor = (ac.output_watts / ac.output_va);
  } else {
    ac.power_factor = 1;
  }
  #ifdef VERBOSE_SERIAL
    sprint("ac.power_factor: ");
    sprintln(ac.power_factor);
  #endif

  ac.output_load_percent = (float)htons(mbusData[13]);
  #ifdef VERBOSE_SERIAL
    sprint("ac.output_load_percent: ");
    sprintln(ac.output_load_percent);
  #endif

  // inverter.charger_source_priority = (float)htons(mbusData[35]);
  // #ifdef VERBOSE_SERIAL
  //   sprint("inverter.charger_source_priority: ");
  //   sprintln(inverter.charger_source_priority);
  // #endif

  // inverter.output_source_priority = (float)htons(mbusData[36]);
  // #ifdef VERBOSE_SERIAL
  //   sprint("inverter.output_source_priority: ");
  //   sprintln(inverter.output_source_priority);
  // #endif

  inverter.charger = (float)htons(mbusData[54]);
  #ifdef VERBOSE_SERIAL
    sprint("inverter.charger: ");
    sprintln(inverter.charger);
  #endif

  inverter.temp = (float)htons(mbusData[56]);
  #ifdef VERBOSE_SERIAL
    sprint("inverter.temp: ");
    sprintln(inverter.temp);
  #endif

  float charge_current_change = -(dc.discharge_current - dc.discharge_current_) 
                                  + (dc.charge_current - dc.charge_current_);
  if (inverter.valid_info && abs(charge_current_change) > 5.0) {
    dc.new_k = (dc.voltage - dc.voltage_) / charge_current_change;
    dc.batt_v_compensation_k += (dc.new_k - dc.batt_v_compensation_k) * 0.1;

    #ifdef VERBOSE_SERIAL
      sprint("dc.new_k: ");
      sprintln(dc.new_k);
    #endif
  }

  #ifdef VERBOSE_SERIAL
    sprint("dc.batt_v_compensation_k: ");
    sprintln(dc.batt_v_compensation_k);
  #endif

  dc.voltage_corrected = dc.voltage - (dc.batt_v_compensation_k * dc.charge_current)
                                    + (dc.batt_v_compensation_k * dc.discharge_current);

  #ifdef VERBOSE_SERIAL
    sprint("dc.voltage_corrected: ");
    sprintln(dc.voltage_corrected);
  #endif

  float soc = 100.0 * (dc.voltage_corrected - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE);
  inverter.soc = (float)constrain(soc, 0, 100);
  #ifdef VERBOSE_SERIAL
    sprint("inverter.soc: ");
    sprintln(inverter.soc);
  #endif

  float input_power = dc.pv_power + dc.discharge_power;
  if (input_power > 0) {
    inverter.eff_w = (100.0 * ac.output_watts) / input_power;
  }
  #ifdef VERBOSE_SERIAL
    sprint("inverter.eff_w: ");
    sprintln(inverter.eff_w);
  #endif
  
  dc.voltage_ = dc.voltage;
  dc.charge_current_ = dc.charge_current;
  dc.discharge_current_ = dc.discharge_current;
  
  inverter.valid_info = 1;

  updateBatteryEnergy(dc.voltage_corrected, dc.charge_current, dc.discharge_current);

  #ifdef VERBOSE_SERIAL
    sprint("inverter.gas_gauge: ");
    sprintln(inverter.gas_gauge);
  #endif

  #ifdef VERBOSE_SERIAL
    sprint("inverter.battery_energy: ");
    sprintln(inverter.battery_energy);
  #endif

  updatePVEnergy(dc.pv_voltage, dc.pv_current, dc.pv_power);

  #ifdef VERBOSE_SERIAL
    sprint("dc.pv_energy_produced: ");
    sprintln(dc.pv_energy_produced);
  #endif

  // AC output energy spent
  static unsigned long lastAcMillis = 0;
  static bool firstAcCall = true;
  updateEnergy(inverter.energy_spent_ac, ac.output_watts, lastAcMillis, firstAcCall);

  #ifdef VERBOSE_SERIAL
    sprint("inverter.energy_spent_ac: ");
    sprintln(inverter.energy_spent_ac);
  #endif

  // Load energy data from Preferences on first successful read
  static bool energy_data_loaded = false;
  if (!energy_data_loaded) {
    loadEnergyData();
    energy_data_loaded = true;
  }

  // Calculate energy source percentages
  inverter.energy_source_ac = 0.0;
  inverter.energy_source_batt = 0.0;
  inverter.energy_source_pv = 0.0;
  
  const float PV_EFFICIENCY = 0.80;
  const float DC_EFFICIENCY = 0.80;
  
  bool has_ac = (ac.input_voltage > 100);
  bool has_pv = (dc.pv_power > 0);
  
  if (ac.output_watts > 0) {
    if (has_ac && !has_pv) {
      inverter.energy_source_ac = 100.0;
      
    } else if (!has_ac && !has_pv && dc.discharge_power > 0) {
      inverter.energy_source_batt = 100.0;
      
    } else if (has_ac && has_pv) {
      float pv_available = dc.pv_power;
      if (dc.charge_power > 0) {
        pv_available -= dc.charge_power;
        if (pv_available < 0) pv_available = 0;
      }
      
      float pv_contribution = pv_available * PV_EFFICIENCY;
      float ac_contribution = ac.output_watts - pv_contribution;
      if (ac_contribution < 0) ac_contribution = 0;
      
      inverter.energy_source_pv = (pv_contribution / ac.output_watts) * 100.0;
      inverter.energy_source_ac = (ac_contribution / ac.output_watts) * 100.0;
      
    } else if (!has_ac && has_pv) {
      float pv_contribution = dc.pv_power * PV_EFFICIENCY;
      float batt_contribution = 0;
      
      if (dc.discharge_power > 0) {
        batt_contribution = dc.discharge_power * DC_EFFICIENCY;
      }
      
      float total = pv_contribution + batt_contribution;
      
      if (total > 0) {
        inverter.energy_source_pv = (pv_contribution / total) * 100.0;
        inverter.energy_source_batt = (batt_contribution / total) * 100.0;
      }
    }
  }
  
  if (inverter.energy_source_ac < 0) inverter.energy_source_ac = 0;
  if (inverter.energy_source_ac > 100) inverter.energy_source_ac = 100;
  if (inverter.energy_source_batt < 0) inverter.energy_source_batt = 0;
  if (inverter.energy_source_batt > 100) inverter.energy_source_batt = 100;
  if (inverter.energy_source_pv < 0) inverter.energy_source_pv = 0;
  if (inverter.energy_source_pv > 100) inverter.energy_source_pv = 100;

  #ifdef VERBOSE_SERIAL
    sprint("inverter.energy_source_ac: ");
    sprintln(inverter.energy_source_ac);
    sprint("inverter.energy_source_batt: ");
    sprintln(inverter.energy_source_batt);
    sprint("inverter.energy_source_pv: ");
    sprintln(inverter.energy_source_pv);
  #endif

  // Save energy data if thresholds exceeded
  saveEnergyData();
}

// OTA settings and mDSN
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

// Function to generate JSON strings
String dataJson() {
    JsonDocument doc;

    JsonObject acObj = doc.createNestedObject("ac");
    acObj["input_voltage"] = ac.input_voltage;
    acObj["input_freq"] = ac.input_freq;
    acObj["output_voltage"] = ac.output_voltage;
    acObj["output_freq"] = ac.output_freq;
    acObj["output_load_percent"] = ac.output_load_percent;
    acObj["power_factor"] = ac.power_factor;
    acObj["output_va"] = ac.output_va;
    acObj["output_watts"] = ac.output_watts;

    JsonObject dcObj = doc.createNestedObject("dc");
    dcObj["voltage"] = dc.voltage;
    dcObj["voltage_corrected"] = dc.voltage_corrected;
    dcObj["charge_power"] = dc.charge_power;
    dcObj["discharge_power"] = dc.discharge_power;
    dcObj["charge_current"] = dc.charge_current;
    dcObj["discharge_current"] = dc.discharge_current;
    dcObj["new_k"] = dc.new_k;
    dcObj["batt_v_compensation_k"] = dc.batt_v_compensation_k;

    JsonObject pvObj = doc.createNestedObject("pv");
    pvObj["pv_voltage"] = dc.pv_voltage;
    pvObj["pv_power"] = dc.pv_power;
    pvObj["pv_current"] = dc.pv_current;
    pvObj["pv_energy_produced"] = dc.pv_energy_produced;

    JsonObject iObj = doc.createNestedObject("inverter");
    iObj["valid_info"] = inverter.valid_info;
    iObj["op_mode"] = inverter.op_mode;
    iObj["soc"] = inverter.soc;
    iObj["gas_gauge"] = inverter.gas_gauge;
    iObj["battery_energy"] = inverter.battery_energy;
    iObj["temp"] = inverter.temp;
    iObj["read_interval_ms"] = dynamic_read_interval;
    iObj["read_time"] = inverter.read_time;
    iObj["read_time_mean"] = inverter.read_time_mean;
    iObj["charger"] = inverter.charger;
    // iObj["charger_source_priority"] = inverter.charger_source_priority;
    iObj["eff_w"] = inverter.eff_w;
    // iObj["output_source_priority"] = inverter.output_source_priority;
    iObj["energy_spent_ac"] = inverter.energy_spent_ac;
    iObj["energy_source_ac"] = inverter.energy_source_ac;
    iObj["energy_source_batt"] = inverter.energy_source_batt;
    iObj["energy_source_pv"] = inverter.energy_source_pv;
    iObj["json_size"] = doc.memoryUsage();
    iObj["uptime"] = uptime();

    if (doc.overflowed()) {
        sprintln("ERROR - Json overflowed");
        sprint("Doc Usage: ");
        sprintln((int)doc.memoryUsage());
    }

    String output;
    serializeJson(doc, output);
    doc.clear();

    return output;
}

// 404 handler >  redir to /
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not Found");
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
  request->send(200, "application/json", dataJson());
  #ifdef VERBOSE_SERIAL
    sprintln("/status");
  #endif
}

// css
void serveCSS(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/style.css");
  #ifdef VERBOSE_SERIAL
    sprintln("/css");
  #endif
}

// js
void serveJS(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/app.js");
  #ifdef VERBOSE_SERIAL
    sprintln("/jscript ");
  #endif
}

// names
void serveNames(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/names.json");
  #ifdef VERBOSE_SERIAL
    sprintln("/names.json ");
  #endif
}


void webserverSetup() {
  server.onNotFound(notFound);
  server.on("/", HTTP_GET, serveIndex);
  server.on("/style.css", HTTP_GET, serveCSS);
  server.on("/app.js", HTTP_GET, serveJS);
  server.on("/api/status", HTTP_GET, serveStatus);
  server.on("/names.json", HTTP_GET, serveNames);

  #ifdef WEBSERIAL
    WebSerial.begin(&server);
    
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

    sprintln("WebSerial Setup");
  #endif

  server.begin();
}

void doWifi() {
  WiFi.setTxPower(WIFI_POWER_5dBm);

  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.begin(c_ssid, c_password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    sprintln("No Wifi Net, back to AP mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_AP);
    delay(50);

    WiFi.softAP(s_ssid, s_password);
    wifiMode = 1;

    myIp = WiFi.softAPIP();
  } else {
    myIp = WiFi.localIP();
    sprintln("Connected to existent Wifi");
    wifiMode = 0;
  }

  sprintln("WiFi Ready");
  sprint("IP address: ");
  sprintln(myIp);
}

void checkWifi() {
  if (WiFi.status() != WL_CONNECTED & wifiMode == 0) {
    doWifi();
  }
}

void wifiScan() {
  sprintln("Start wifi Scan for the AP");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    sprintln("no networks found");
  } else {
    for (int i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == c_ssid) {
        sprintln("AP found, try to conenct");
        doWifi();
      } else {
        sprintln("No networks found");
      }
    }
  }
}

void idle() {
    timer.run();
    yield();
}

void nodeSetup() {
  // Initialize serial based on configuration
  Serial1.begin(2400, SERIAL_8N1, RXD2, TXD2);
  sprintln("Using Hardware Serial1");
  if (Serial1) {
    sprintln("Serial1 init ok");
  } else {
    sprintln("Serial1 init problem !!!");
  }

  node.begin(5, Serial1);
  node.idle(idle);

  timer.setInterval(dynamic_read_interval, sendRequest);
}

void setup() {
  Serial.begin (MONITOR_SERIAL_SPEED);

  doWifi();

  timer.setInterval(3*60*1000, checkWifi);

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

  sprintln("Ready to rock...");
}

void loop() {
  ArduinoOTA.handle();

  timer.run();

  delay(1);
}
