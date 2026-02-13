// Modbus communication header
// Modularized from main.cpp

#ifndef MODBUS_H
#define MODBUS_H

#include <Arduino.h>
#include <ModbusMaster.h>

// Initialize Modbus
void nodeSetup();

// Read inverter data
void sendRequest();

// Internal: read registers in chunks
uint8_t readRegistersChunked(uint16_t startAddr, uint16_t totalRegs, uint16_t *data);

// Idle callback
void idle();

#endif // MODBUS_H
