#include "WebApi.h"
#include <ElegantOTA.h>
#include <Update.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Idle-task watchdogs off during OTA. The C3 is single-core (no core 1).
static void wdtOff() {
    disableCore0WDT();
#if !CONFIG_FREERTOS_UNICORE
    disableCore1WDT();
#endif
}
static void wdtOn() {
    enableCore0WDT();
#if !CONFIG_FREERTOS_UNICORE
    enableCore1WDT();
#endif
}

// OTA safety handling copied from FG1's LocalServer (bench-proven there):
// watchdogs + brownout widened for the flash-erase window, stall abort.
static uint32_t s_savedBrownout = 0;
static uint32_t s_lastOtaProgress = 0;
static const uint32_t OTA_STALL_MS = 20000;

void WebApi::begin() {
    _ws.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type,
                       void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.printf("[WEB] WS client #%u from %s\n", c->id(), c->remoteIP().toString().c_str());
            c->text("{\"type\":\"status\",\"data\":" + _lastStatus + "}");
        } else if (type == WS_EVT_DATA) {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                String msg((const char*)data, len);
                c->text(_dispatch(msg));
            }
        }
    });
    _server.addHandler(&_ws);

    _server.on("/status", HTTP_GET, [this](AsyncWebServerRequest* r) {
        r->send(200, "application/json", _lastStatus);
    });
    _server.on("/command", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
        [this](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) { body = ""; body.reserve(total); }
            body.concat((const char*)data, len);
            if (index + len == total) r->send(200, "application/json", _dispatch(body));
        });

    ElegantOTA.begin(&_server);
    ElegantOTA.setAuth(OTA_USERNAME, OTA_PASSWORD);
    ElegantOTA.onStart([]() {
        Serial.println("[WEB] OTA start");
        wdtOff();
        s_savedBrownout = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
        s_lastOtaProgress = millis();
    });
    ElegantOTA.onProgress([](size_t, size_t) { s_lastOtaProgress = millis(); });
    ElegantOTA.onEnd([](bool ok) {
        Serial.printf("[WEB] OTA %s\n", ok ? "done — rebooting" : "failed");
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, s_savedBrownout);
        wdtOn();
    });

    _server.begin();
    Serial.println("[WEB] HTTP+WS on port 80, OTA at /update");
}

void WebApi::loop() {
    _ws.cleanupClients();
    ElegantOTA.loop();
    if (Update.isRunning() && millis() - s_lastOtaProgress > OTA_STALL_MS) {
        Serial.println("[WEB] OTA stalled — aborting");
        Update.abort();
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, s_savedBrownout);
        wdtOn();
    }
}

void WebApi::push(const char* type, const String& json) {
    if (_ws.count() == 0) return;
    _ws.textAll("{\"type\":\"" + String(type) + "\",\"data\":" + json + "}");
}

String WebApi::_dispatch(const String& body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return "{\"ok\":false,\"error\":\"invalid JSON\"}";
    const char* cmd = doc["cmd"];
    if (!cmd) return "{\"ok\":false,\"error\":\"no cmd\"}";
    if (!onCommand) return "{\"ok\":false,\"error\":\"not ready\"}";
    String err;
    String data = onCommand(String(cmd), doc.as<JsonObject>(), err);
    JsonDocument out;
    out["ok"] = err.isEmpty();
    out["cmd"] = cmd;
    if (!doc["id"].isNull()) out["id"] = doc["id"];    // app matches replies by id
    if (!err.isEmpty()) out["error"] = err;
    String s;
    serializeJson(out, s);
    // splice raw data JSON in without re-parsing it
    s.remove(s.length() - 1);
    s += ",\"data\":" + (data.isEmpty() ? String("{}") : data) + "}";
    return s;
}
