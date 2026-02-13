// Energy and battery calculations implementation
// Modularized from main.cpp

#include "energy.h"
#include "globals.h"
#include "utils.h"

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

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

// Update battery energy based on voltage and current
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
  
  if (inverter.battery_energy > 0) {
    inverter.gas_gauge = (inverter.battery_energy * 100) / MAXIMUM_ENERGY;
    
    if (inverter.gas_gauge < 0.0) inverter.gas_gauge = 0.0;
    if (inverter.gas_gauge > 100.0) inverter.gas_gauge = 100.0;
  }
}

// Update PV energy produced
void updatePVEnergy(float pvVoltage, float pvCurrent, float pvPower) {
  static unsigned long lastPvMillis = 0;
  static bool firstPvCall = true;
  static unsigned long nightStartMillis = 0;
  static bool isNight = false;
  static bool sixHourDarknessPassed = false;
  static bool sunriseDetected = false;
  static float previousPvVoltage = 0.0;

  unsigned long currentMillis = millis();
  
  if (pvVoltage <= 30) {
    // Night time (PV voltage below threshold)
    if (!isNight) {
      isNight = true;
      nightStartMillis = currentMillis;
      sixHourDarknessPassed = false;
      sunriseDetected = false;
    } else {
      unsigned long nightDuration;
      if (currentMillis < nightStartMillis) {
        nightDuration = (0xFFFFFFFF - nightStartMillis) + currentMillis + 1;
      } else {
        nightDuration = currentMillis - nightStartMillis;
      }

      if (nightDuration >= (6*3600000) && !sixHourDarknessPassed) {
        sixHourDarknessPassed = true;
        Serial.println("Night detected (6h darkness) - Ready for sunrise reset");
      }
    }

    previousPvVoltage = pvVoltage;
    lastPvMillis = currentMillis;
    return;
  } else {
    // Day time (PV voltage above threshold)
    if (isNight) {
      if (previousPvVoltage <= 30 && pvVoltage > 30) {
        sunriseDetected = true;
        sprint("==> Sunrise detected");
        
        if (sixHourDarknessPassed) {
          dc.pv_energy_produced = 0.0;
          sixHourDarknessPassed = false;
          Serial.println("Sunrise after 6h darkness - PV energy reset to 0");
        } else {
          sprint("Sunrise before 6h darkness - keeping energy data");
        }
      }
      isNight = false;
    }
  }

  previousPvVoltage = pvVoltage;
  float powerToUse = (pvPower > 0.0) ? pvPower : (pvVoltage * pvCurrent);
  updateEnergy(dc.pv_energy_produced, powerToUse, lastPvMillis, firstPvCall);
}

// Calculate battery autonomy in minutes using EWMA
void calculateAutonomy() {
  if (inverter.energy_source_batt > 0 && ac.output_watts > 0 && inverter.eff_w > 0) {
    float autonomy_alpha = calculateDynamicAlpha();

    float capped_efficiency = (inverter.eff_w < AUTONOMY_EFFICIENCY_CAP) ? inverter.eff_w : AUTONOMY_EFFICIENCY_CAP;

    if (!autonomy_initialized) {
      autonomy_efficiency_ewma = capped_efficiency;
      autonomy_watts_ewma = ac.output_watts;
      autonomy_initialized = true;
    } else {
      calculateEWMA(autonomy_efficiency_ewma, capped_efficiency, autonomy_alpha);
      calculateEWMA(autonomy_watts_ewma, ac.output_watts, autonomy_alpha);
    }

    float dc_watts = autonomy_watts_ewma / (autonomy_efficiency_ewma / 100.0);

    float hours_remaining = 0.0;
    if (dc_watts > 0) {
      hours_remaining = inverter.battery_energy / dc_watts;
    }

    unsigned int minutes_remaining = (unsigned int)(hours_remaining * 60.0);
    unsigned int max_minutes = AUTONOMY_MAX_DAYS * 24 * 60;

    inverter.autonomy = min(minutes_remaining, max_minutes);

    #ifdef VERBOSE_SERIAL
      sprint("Autonomy EWMA (α=");
      sprint(autonomy_alpha, 3);
      sprint(") - Eff: ");
      sprint(autonomy_efficiency_ewma, 1);
      sprint("%, AC Watts: ");
      sprint(autonomy_watts_ewma, 1);
      sprint(", DC Watts: ");
      sprint(dc_watts, 1);
      sprint(", Hours left: ");
      sprint(hours_remaining, 1);
      sprint(" (");
      sprint(inverter.autonomy);
      sprintln(" min)");
    #endif
  } else {
    inverter.autonomy = AUTONOMY_MAX_DAYS * 24 * 60;
  }
}

// Load energy data from Preferences
void loadEnergyData() {
  prefs.begin("energy_data", true);

  if (prefs.isKey("pv_energy")) {
    dc.pv_energy_produced = prefs.getFloat("pv_energy", 0.0);
    inverter.battery_energy = prefs.getFloat("batt_energy", 0.0);
    inverter.gas_gauge = prefs.getFloat("gas_gauge", 0.0);
    inverter.energy_spent_ac = prefs.getFloat("ac_energy", 0.0);

    sprintln("==> Loaded energy data from Preferences:");
    sprint("  PV energy: ");
    sprintln(dc.pv_energy_produced);
    sprint("  Battery energy: ");
    sprintln(inverter.battery_energy);
    sprint("  Gas gauge: ");
    sprintln(inverter.gas_gauge);
    sprint("  AC energy spent: ");
    sprintln(inverter.energy_spent_ac);
  } else {
    dc.pv_energy_produced = 0.0;
    inverter.energy_spent_ac = 0.0;

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
void saveEnergyData(bool force) {
  static float last_pv = 0.0;
  static float last_batt = 0.0;
  static float last_gg = 0.0;
  static float last_ac = 0.0;
  static bool first_call = true;

  if (first_call) {
    last_pv = dc.pv_energy_produced;
    last_batt = inverter.battery_energy;
    last_gg = inverter.gas_gauge;
    last_ac = inverter.energy_spent_ac;
    first_call = false;
    return;
  }

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

  if (should_save || force) {
    prefs.begin("energy_data", false);

    prefs.putFloat("pv_energy", dc.pv_energy_produced);
    prefs.putFloat("batt_energy", inverter.battery_energy);
    prefs.putFloat("gas_gauge", inverter.gas_gauge);
    prefs.putFloat("ac_energy", inverter.energy_spent_ac);

    prefs.end();

    last_pv = dc.pv_energy_produced;
    last_batt = inverter.battery_energy;
    last_gg = inverter.gas_gauge;
    last_ac = inverter.energy_spent_ac;

    #ifdef VERBOSE_SERIAL
      sprintln("==> Energy data saved to Preferences");
    #endif
  }
}
