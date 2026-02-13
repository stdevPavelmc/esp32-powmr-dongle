// Modbus communication implementation
// Modularized from main.cpp

#include "modbus.h"
#include "globals.h"
#include "utils.h"
#include "energy.h"

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// Idle callback for Modbus
void idle() {
  delay(1);
  yield();
}

// Initialize Modbus serial connection
void nodeSetup() {
  Serial1.begin(2400, SERIAL_8N1, RXD2, TXD2);
  sprintln("Using Hardware Serial1");
  if (Serial1) {
    sprintln("Serial1 init ok");
  } else {
    sprintln("Serial1 init problem !!!");
  }

  node.begin(5, Serial1);
  node.idle(idle);
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

// Main function to read inverter data via Modbus
void sendRequest() {
  sprintln("==> Reading registers 4501-4561 (61 regs)");
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
    }
    
    return;
  }

  consecutive_failures = 0;

  unsigned long stop = millis();
  if (stop > start) {
    stop -= start;
    inverter.read_time = (float)stop / 1000.0;

    if (!read_time_initialized) {
      read_time_initialized = true;
      inverter.read_time_mean = inverter.read_time;
    } else {
      calculateEWMA(inverter.read_time_mean, inverter.read_time, calculateDynamicAlpha());
    }
    
    #ifdef VERBOSE_SERIAL
      sprint("inverter.read_time: ");
      sprintln(inverter.read_time);
      sprint("inverter.read_time_mean: ");
      sprintln(inverter.read_time_mean);
    #endif
  }

  float new_interval = calculateNextInterval();
  if (new_interval != dynamic_read_interval) {
    dynamic_read_interval = new_interval;
    
    #ifdef VERBOSE_SERIAL
      sprint("Adjusting read interval to: ");
      sprint(dynamic_read_interval, 2);
      sprintln(" s");
    #endif
  }

  // Parse register data
  inverter.op_mode = (float)htons(mbusData[0]);

  ac.input_voltage = htons(mbusData[1]) / 10.0;
  ac.input_freq = htons(mbusData[2]) / 10.0;

  dc.pv_voltage = htons(mbusData[3]) / 10.0;
  if (dc.pv_voltage < 6) {
    dc.pv_voltage = 0;
  }

  dc.pv_power = (float)htons(mbusData[4]);
  if (dc.pv_voltage < 6) {
    dc.pv_power = 0;
  }

  if (dc.pv_voltage > 0) {
    dc.pv_current = dc.pv_power / dc.pv_voltage;
  } else {
    dc.pv_current = 0;
  }

  dc.voltage = htons(mbusData[5]) / 10.0;

  dc.charge_current = (float)htons(mbusData[7]);
  dc.discharge_current = (float)htons(mbusData[8]);

  dc.discharge_power = dc.voltage * dc.discharge_current;
  dc.charge_power = dc.voltage * dc.charge_current;

  ac.output_voltage = htons(mbusData[9]) / 10.0;
  ac.output_freq = htons(mbusData[10]) / 10.0;
  ac.output_va = (float)htons(mbusData[11]);
  ac.output_watts = (float)htons(mbusData[12]);

  if (ac.output_watts > 0 & ac.output_va > 0) {
    ac.power_factor = (ac.output_watts / ac.output_va);
  } else {
    ac.power_factor = 1;
  }

  ac.output_load_percent = (float)htons(mbusData[13]);

  inverter.charger = (float)htons(mbusData[54]);
  inverter.temp = (float)htons(mbusData[56]);

  // Battery voltage compensation
  float charge_current_change = -(dc.discharge_current - dc.discharge_current_) 
                                + (dc.charge_current - dc.charge_current_);
  if (inverter.valid_info && abs(charge_current_change) > 5.0) {
    dc.new_k = (dc.voltage - dc.voltage_) / charge_current_change;
    dc.batt_v_compensation_k += (dc.new_k - dc.batt_v_compensation_k) * 0.1;
  }

  dc.voltage_corrected = dc.voltage - (dc.batt_v_compensation_k * dc.charge_current)
                                    + (dc.batt_v_compensation_k * dc.discharge_current);

  float soc = 100.0 * (dc.voltage_corrected - BATT_MIN_VOLTAGE) / (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE);
  inverter.soc = (float)constrain(soc, 0, 100);

  float input_power = dc.pv_power + dc.discharge_power;
  if (input_power > 0) {
    inverter.eff_w = (100.0 * ac.output_watts) / input_power;
  }
  
  dc.voltage_ = dc.voltage;
  dc.charge_current_ = dc.charge_current;
  dc.discharge_current_ = dc.discharge_current;
  
  inverter.valid_info = 1;

  // Update energy calculations
  updateBatteryEnergy(dc.voltage_corrected, dc.charge_current, dc.discharge_current);
  updatePVEnergy(dc.pv_voltage, dc.pv_current, dc.pv_power);

  // AC output energy spent
  static unsigned long lastAcMillis = 0;
  static bool firstAcCall = true;
  updateEnergy(inverter.energy_spent_ac, ac.output_watts, lastAcMillis, firstAcCall);

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

  // Calculate battery autonomy
  calculateAutonomy();

  // Save energy data if thresholds exceeded
  saveEnergyData();
}
