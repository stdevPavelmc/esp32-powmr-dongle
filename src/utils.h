// Utility functions header
// Modularized from main.cpp

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

// Timing utilities
unsigned int uptime();
bool hasTimeElapsed(unsigned long &lastTime, unsigned long currentTime, unsigned long interval);
float calculateNextInterval();
float calculateDynamicAlpha();

// EWMA calculation
void calculateEWMA(float &avg, float newVal, float alpha);

#endif // UTILS_H
