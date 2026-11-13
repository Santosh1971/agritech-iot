#pragma once
// Local HTTP + WebSocket API (SoftAP 192.168.4.1, or the farm LAN IP).
// Same message shapes as FG1's LocalServer so the app code stays familiar:
//   app -> device   {"cmd":"...", ...}            (WS text or POST /command)
//   device -> app   {"ok":true,"cmd":"...","data":...}   reply to a command
//                   {"type":"status","data":{...}}       pushed every 5 s
//   GET /status     latest status JSON
//   /update         ElegantOTA (auth OTA_USERNAME / OTA_PASSWORD)
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

class WebApi {
public:
    void begin();
    void loop();
    bool hasClients() { return _ws.count() > 0; }
    void closeAll()   { _ws.closeAll(); }
    void push(const char* type, const String& json);   // broadcast to all WS clients
    void setStatus(const String& json) { _lastStatus = json; push("status", json); }

    // Return the JSON *data* for a command; may set ok=false via the error string.
    std::function<String(const String& cmd, JsonObject payload, String& err)> onCommand;

private:
    String _dispatch(const String& body);
    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};
    String _lastStatus = "{}";
};
