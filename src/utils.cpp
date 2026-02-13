// Utility functions implementation
// Modularized from main.cpp

#include "utils.h"
#include "globals.h"
#include <Arduino.h>

// Get uptime in seconds
unsigned int uptime() {
  unsigned long uptime_sec = millis() / 1000;
  return (unsigned int)(uptime_sec);
}

// Check if time interval has elapsed, handling millis() rollover
bool hasTimeElapsed(unsigned long &lastTime, unsigned long currentTime, unsigned long interval) {
  unsigned long timeDiff;
  if (currentTime < lastTime) {
    // Rollover occurred
    timeDiff = (0xFFFFFFFF - lastTime) + currentTime + 1;
  } else {
    timeDiff = currentTime - lastTime;
  }
  return (timeDiff >= interval);
}

// Calculate next read interval based on average read time
float calculateNextInterval() {
  if (inverter.read_time_mean == 0.0) {
    return (float)(INITIAL_READ_INTERVAL);
  }
  
  // Round up to next 5 second multiple
  float base = int(inverter.read_time_mean / 5) * 5;
  if (fmod(inverter.read_time_mean, 5.0) != 0.0) {
    base += 5.0;
  }
  
  // Ensure minimum of 5 seconds max of 30 seconds
  if (base < 5.0) {
    base = 5.0;
  }
  if (base > 30.0) {
    base = 30.0;
  }
  
  return base;
}

// Calculate dynamic alpha for EWMA based on 5-minute window
float calculateDynamicAlpha() {
  float readings_per_minute = 60.0 / dynamic_read_interval;
  float readings_in_window = readings_per_minute * AUTONOMY_WINDOW_MINUTES;

  float alpha = 2.0 / (readings_in_window + 1.0);
  return constrain(alpha, 0.01, 0.5);
}

// Exponentially Weighted Moving Average
void calculateEWMA(float &avg, float newVal, float alpha) {
  float temp_avg = alpha * newVal + (1.0 - alpha) * avg;
  avg = temp_avg;
}
