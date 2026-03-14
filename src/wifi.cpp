// WiFi management implementation
// Modularized from main.cpp

#include "wifi.h"
#include "globals.h"
#include "wifi_creds.h"
#include <WiFi.h>
#include <WiFiAP.h>

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// Reconnection interval when in AP mode (2 minutes in milliseconds)
#define AP_RECONNECT_INTERVAL_MS 120000

// Connect to WiFi or start AP mode
void doWifi() {
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
    apModeStartTime = millis();

    // Persist AP mode state to Preferences
    prefs.putBool("ap_mode", true);
    prefs.putULong("ap_start_time", apModeStartTime);

    myIp = WiFi.softAPIP();
  } else {
    myIp = WiFi.localIP();
    sprintln("Connected to existent Wifi");
    wifiMode = 0;
    apModeStartTime = 0;

    // Clear AP mode state from Preferences
    prefs.putBool("ap_mode", false);

    sprintln("WiFi Ready");
    sprint("IP address: ");
    sprintln(myIp);
  }
}

// Check WiFi connection and reconnect if needed
void checkWifi() {
  if (WiFi.status() != WL_CONNECTED && wifiMode == 0) {
    doWifi();
  }
}

// Attempt to reconnect to client WiFi when in AP mode
// Called periodically to check if client network is available
void tryReconnectToClient() {
  // Only attempt reconnection if in AP mode
  if (wifiMode != 1) {
    return;
  }

  unsigned long currentTime = millis();
  
  // Check if 2 minutes have passed since entering AP mode or last attempt
  if (currentTime - apModeStartTime >= AP_RECONNECT_INTERVAL_MS) {
    sprintln("AP mode: Attempting to reconnect to client network...");
    
    // Try to connect to client network
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.begin(c_ssid, c_password);
    
    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
      // Successfully connected to client network
      myIp = WiFi.localIP();
      sprintln("Successfully reconnected to client network");
      sprintln("Exiting AP mode");
      wifiMode = 0;
      apModeStartTime = 0;
      
      // Clear AP mode state from Preferences
      prefs.putBool("ap_mode", false);
      
      sprintln("WiFi Ready");
      sprint("IP address: ");
      sprintln(myIp);
    } else {
      // Failed to connect, return to AP mode
      sprintln("Failed to connect to client network, returning to AP mode");
      WiFi.disconnect();
      WiFi.mode(WIFI_OFF);
      delay(50);
      WiFi.mode(WIFI_AP);
      delay(50);
      WiFi.softAP(s_ssid, s_password);
      
      // Reset the timer for next attempt
      apModeStartTime = millis();
      myIp = WiFi.softAPIP();
      
      // Update persistent state
      prefs.putULong("ap_start_time", apModeStartTime);
    }
  }
}

// Initialize WiFi mode from persistent storage
void initWifiMode() {
  // Check if we were in AP mode before reboot
  bool wasApMode = prefs.getBool("ap_mode", false);
  
  if (wasApMode) {
    sprintln("Previous session ended in AP mode, starting in AP mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_AP);
    delay(50);
    WiFi.softAP(s_ssid, s_password);
    wifiMode = 1;
    apModeStartTime = prefs.getULong("ap_start_time", millis());
    myIp = WiFi.softAPIP();
  } else {
    // Start normal WiFi connection
    doWifi();
  }
}
