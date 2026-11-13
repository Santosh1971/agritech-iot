#pragma once
// MQTT link to mqtt.agrisenseandcontrol.in — same task-based design as FG1's
// MQTTHandler (all network I/O in its own FreeRTOS task on core 1, so a slow
// broker or DNS never stalls the misting loop).
//
// Topics: agrisense/FG1/<deviceId>/   (deviceId = FG1P_xxxx)
//   status    device -> app, retained, every 5 s
//   resp      device -> app, replies to commands {"cmd","ok","error","data"}
//   lwt       retained {"online":true|false}
//   command   app -> device, {"cmd":...}
//   settings  app -> device, RETAINED set_settings, so a change made while
//             the controller is offline is applied when it reconnects
//             (only if its "rev" is newer than what the controller has).
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"

class Cloud {
public:
    void begin(const String& deviceId);
    void loop();                                  // dispatch queued commands (Arduino loop task)
    bool connected() const { return _connected; }
    void publishStatus(const String& json) { _enqueue(0, json); }
    void publishResp(const String& json)   { _enqueue(1, json); }

    std::function<void(const String& cmd, JsonObject payload)> onCommand;
    void _incoming(const byte* p, unsigned len);  // MQTT task context

private:
    struct Out { uint8_t topic; char* payload; };
    static void _taskEntry(void* a) { static_cast<Cloud*>(a)->_task(); }
    void _task();
    void _reconnect();
    void _drain();
    bool _enqueue(uint8_t topic, const String& json);

    WiFiClient   _wifi;
    PubSubClient _mqtt;
    QueueHandle_t _outQ = nullptr, _inQ = nullptr;
    volatile bool _connected = false;
    uint32_t _lastTry = 0, _backoff = 0;
    String _id, _tStatus, _tResp, _tLwt, _tCmd, _tSettings;
};
