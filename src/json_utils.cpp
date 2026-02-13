// JSON utilities implementation
// Modularized from main.cpp

#include "json_utils.h"
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

// Generate JSON string from inverter data
String dataJson() {
    JsonDocument doc;

    JsonObject acObj = doc["ac"].to<JsonObject>();
    acObj["input_voltage"] = ac.input_voltage;
    acObj["input_freq"] = ac.input_freq;
    acObj["output_voltage"] = ac.output_voltage;
    acObj["output_freq"] = ac.output_freq;
    acObj["output_load_percent"] = ac.output_load_percent;
    acObj["power_factor"] = ac.power_factor;
    acObj["output_va"] = ac.output_va;
    acObj["output_watts"] = ac.output_watts;

    JsonObject dcObj = doc["dc"].to<JsonObject>();
    dcObj["voltage"] = dc.voltage;
    dcObj["voltage_corrected"] = dc.voltage_corrected;
    dcObj["charge_power"] = dc.charge_power;
    dcObj["discharge_power"] = dc.discharge_power;
    dcObj["charge_current"] = dc.charge_current;
    dcObj["discharge_current"] = dc.discharge_current;
    dcObj["new_k"] = dc.new_k;
    dcObj["batt_v_compensation_k"] = dc.batt_v_compensation_k;

    JsonObject pvObj = doc["pv"].to<JsonObject>();
    pvObj["pv_voltage"] = dc.pv_voltage;
    pvObj["pv_power"] = dc.pv_power;
    pvObj["pv_current"] = dc.pv_current;
    pvObj["pv_energy_produced"] = dc.pv_energy_produced;

    JsonObject iObj = doc["inverter"].to<JsonObject>();
    iObj["valid_info"] = inverter.valid_info;
    iObj["op_mode"] = inverter.op_mode;
    iObj["soc"] = inverter.soc;
    iObj["gas_gauge"] = inverter.gas_gauge;
    iObj["battery_energy"] = inverter.battery_energy;
    iObj["temp"] = inverter.temp;
    iObj["read_interval"] = dynamic_read_interval;
    iObj["read_time"] = inverter.read_time;
    iObj["read_time_mean"] = inverter.read_time_mean;
    iObj["charger"] = inverter.charger;
    iObj["eff_w"] = inverter.eff_w;
    iObj["energy_spent_ac"] = inverter.energy_spent_ac;
    iObj["energy_source_ac"] = inverter.energy_source_ac;
    iObj["energy_source_batt"] = inverter.energy_source_batt;
    iObj["energy_source_pv"] = inverter.energy_source_pv;
    iObj["autonomy"] = inverter.autonomy;
    iObj["json_size"] = measureJson(doc);
    iObj["uptime"] = uptime();

    if (doc.overflowed()) {
        sprintln("ERROR - Json overflowed");
        sprint("Doc Usage: ");
        sprintln((int)measureJson(doc));
    }

    String output;
    serializeJson(doc, output);
    doc.clear();

    return output;
}
