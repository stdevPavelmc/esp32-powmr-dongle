// WiFi credentials - DO NOT COMMIT THIS FILE
// Copy from template and fill in your credentials

#ifndef WIFI_CREDS_H
#define WIFI_CREDS_H

// Client WiFi settings (for normal operation)
static const char *c_ssid = "Your_WiFi_Network_Name";
static const char *c_password = "Your_WiFi_Password";

// AP fallback settings (when client WiFi unavailable)
static const char *s_ssid = "ESP32-PowMr";
static const char *s_password = "Your_AP_Password";

// Hostname for mDNS and network discovery
static const char *hostname = "ESP32-PowMr";

#endif // WIFI_CREDS_H
