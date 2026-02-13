// Data structures for inverter monitoring
// Modularized from main.cpp

#ifndef DATA_H
#define DATA_H

#include "config.h"

// AC data structure
struct ACData {
  float input_voltage;
  float input_freq;
  float output_voltage;
  float output_freq;
  float output_va;
  float output_watts;
  float output_load_percent;
  float power_factor;
};

// DC data structure
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

// Inverter data structure
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
   float read_time = 0.0;
   float read_time_mean = 0.0;
   float battery_energy = 0.0;
   float gas_gauge = 0.0;
   float energy_spent_ac = 0.0;
   float energy_source_ac = 0.0;
   float energy_source_batt = 0.0;
   float energy_source_pv = 0.0;
   unsigned int autonomy = AUTONOMY_MAX_DAYS * 24 * 60;  // Autonomy in minutes
};

#endif // DATA_H
