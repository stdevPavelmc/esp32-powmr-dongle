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

// Check WiFi connection and reconnect if needed
void checkWifi() {
  if (WiFi.status() != WL_CONNECTED & wifiMode == 0) {
    doWifi();
  }
}
