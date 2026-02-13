// Energy and battery calculations header
// Modularized from main.cpp

#ifndef ENERGY_H
#define ENERGY_H

#include <Arduino.h>

// Generic energy accumulation
void updateEnergy(float &energy, float power, unsigned long &lastMillis, bool &firstCall);

// Battery energy calculations
void updateBatteryEnergy(float voltage, float chargeCurrent, float dischargeCurrent);

// PV energy calculations
void updatePVEnergy(float pvVoltage, float pvCurrent, float pvPower);

// Autonomy calculation
void calculateAutonomy();

// Persistence
void loadEnergyData();
void saveEnergyData(bool force = false);

#endif // ENERGY_H
