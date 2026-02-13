// Web server implementation
// Modularized from main.cpp

#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <SPIFFS.h>
#include <FS.h>
#include "webserver.h"
#include "globals.h"
#include "json_utils.h"

// Print macros for this module
#ifdef WEBSERIAL
  #include <WebSerial.h>
  #define sprint(...) WebSerial.print(__VA_ARGS__)
  #define sprintln(...) WebSerial.println(__VA_ARGS__)
#else
  #define sprint(...) Serial.print(__VA_ARGS__)
  #define sprintln(...) Serial.println(__VA_ARGS__)
#endif

// 404 handler - redirect to /
void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not Found");
}

// Serve index.html
void serveIndex(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html");
  #ifdef VERBOSE_SERIAL
    sprintln("/ ");
  #endif
}

// Serve status JSON
void serveStatus(AsyncWebServerRequest *request) {
  request->send(200, "application/json", dataJson());
  #ifdef VERBOSE_SERIAL
    sprintln("/status");
  #endif
}

// Serve style.css
void serveCSS(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/style.css");
  #ifdef VERBOSE_SERIAL
    sprintln("/css");
  #endif
}

// Serve app.js
void serveJS(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/app.js");
  #ifdef VERBOSE_SERIAL
    sprintln("/jscript ");
  #endif
}

// Serve names.json
void serveNames(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/names.json");
  #ifdef VERBOSE_SERIAL
    sprintln("/names.json ");
  #endif
}

// Initialize web server
void webserverSetup() {
  server.onNotFound(notFound);
  server.on("/", HTTP_GET, serveIndex);
  server.on("/style.css", HTTP_GET, serveCSS);
  server.on("/app.js", HTTP_GET, serveJS);
  server.on("/api/status", HTTP_GET, serveStatus);
  server.on("/names.json", HTTP_GET, serveNames);

  #ifdef WEBSERIAL
    WebSerial.begin(&server);
    
    WebSerial.onMessage([&](uint8_t *data, size_t len) {
      Serial.printf("Received %u bytes from WebSerial: ", len);
      Serial.write(data, len);
      Serial.println();
      WebSerial.println("Received Data...");
      String d = "";
      for(size_t i=0; i < len; i++){
        d += char(data[i]);
      }
      WebSerial.println(d);
    });

    sprintln("WebSerial Setup");
  #endif

  server.begin();
}
