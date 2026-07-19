#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "Config.h"

// Always-on local server — reachable over whichever interface is currently
// up (SoftAP, STA/home-WiFi, or both concurrently). Two jobs, both replacing
// what used to be two separate transports:
//
//   1. Provisioning (formerly BLE): wifi_config, mqtt_config, rtc_sync,
//      calibrate, relay_test, factory_reset, wifi_scan, device_info.
//      Same command names/fields BLE used, now sent as HTTP/WS JSON.
//
//   2. Operational control (formerly MQTT-only): stop_cycle, manual_on,
//      set_cycles, get_history_range, etc. — reaches the device locally
//      whether or not the cloud/MQTT path is currently up.
//
// onCommand returns a JSON String response (unlike the old MQTT-only
// handler, which was fire-and-forget) so provisioning commands like
// device_info/wifi_scan can reply synchronously over HTTP or WS.
class LocalServer {
public:
    void begin();
    void loop();

    bool isConnected();      // true if >=1 WS client attached
    int  stationCount();     // phones associated to our SoftAP, if it's up

    // Proactively drops every WS client. cleanupClients() (called every
    // loop() below) only enforces a max client count — it does NOT detect
    // a client that's gone silently unresponsive (e.g. a phone that was
    // attached to our SoftAP and then the STA link came back up and the
    // app moved on to MQTT without ever cleanly closing that socket).
    // That left a "zombie" client whose outgoing message queue just kept
    // growing every status broadcast until it overflowed — seen in
    // testing as a long stretch of "[/ws][1] Too many messages queued"
    // warnings, and very likely the cause of the sluggishness reported
    // in Cloud/MQTT mode specifically. Call this right when the STA link
    // comes back up — any local WS client at that point is almost
    // certainly stale, since real local usage would be over the SoftAP
    // which is about to go away anyway.
    void closeAllClients();

    void publishStatus(const String& json);
    void publishActiveCycle(const String& json);
    void publishCycles(const String& json);
    bool publishHistory(const String& json);

    std::function<String(const String& cmd, const JsonObject& payload)> onCommand;

private:
    String _dispatch(const String& msg);   // shared by WS + HTTP POST paths
    void   _broadcast(const char* type, const String& json);

    AsyncWebServer _server{80};
    AsyncWebSocket _ws{"/ws"};

    String _lastStatus  = "{}";
    String _lastActive  = "{}";
    String _lastCycles  = "[]";
    String _lastHistory = "[]";
};
