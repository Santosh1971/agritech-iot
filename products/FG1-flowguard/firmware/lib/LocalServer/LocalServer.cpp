#include "LocalServer.h"
#include <ElegantOTA.h>
#include <Update.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Saved/restored around the OTA window — see ElegantOTA.onStart()/onEnd()
// below. Not a global disable: this only widens the safety margin for the
// few seconds flash erase can hog a core, then puts it straight back.
static uint32_t s_savedBrownoutReg = 0;

// Bench-verified 2026-09-14: a weak WiFi link (client TCP connection dying
// without a clean FIN reaching us) leaves Update.isRunning() stuck true
// forever — onEnd() only fires on a clean finish/error, never on a client
// that just vanishes. With watchdogs and the brownout detector both still
// disabled from onStart() (see above), that's an open-ended safety gap, not
// just a stuck upload. Worse, the whole local server hung — not just OTA —
// for the ~30s it took AsyncTCP's own (deliberately raised) timeouts to
// notice the dead socket on their own. Track last-progress time and force
// an abort proactively instead of waiting on that generic timeout.
static uint32_t s_lastOtaProgressMs = 0;
static const uint32_t OTA_STALL_TIMEOUT_MS = 20000;

void LocalServer::begin() {
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.printf("[LocalServer] WS client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            // Push current state immediately so the app doesn't wait for
            // the next periodic publish.
            client->text("{\"type\":\"status\",\"data\":" + _lastStatus + "}");
            client->text("{\"type\":\"cycles\",\"data\":" + _lastCycles + "}");
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.printf("[LocalServer] WS client #%u disconnected\n", client->id());
        } else if (type == WS_EVT_DATA) {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                String msg;
                msg.reserve(len);
                for (size_t i = 0; i < len; i++) msg += (char)data[i];
                String resp = _dispatch(msg);
                client->text(resp);
            }
        }
    });
    _server.addHandler(&_ws);

    _server.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "application/json", _lastStatus);
    });
    _server.on("/cycles", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "application/json", _lastCycles);
    });
    _server.on("/history", HTTP_GET, [this](AsyncWebServerRequest* req) {
        req->send(200, "application/json", _lastHistory);
    });

    // POST /command — same {"cmd": "...", ...} schema used over BLE/MQTT.
    // Body callback (3rd lambda) does the real work; the 2nd lambda handles
    // requests with no body (not expected here, but required by the API).
    // Accumulates across chunks (AsyncWebServer may split larger bodies
    // like set_cycles into multiple calls) and only dispatches/responds
    // once the full body has arrived.
    _server.on("/command", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            static String body;
            if (index == 0) body = "";
            body.reserve(total);
            for (size_t i = 0; i < len; i++) body += (char)data[i];
            if (index + len == total) {
                String resp = _dispatch(body);
                req->send(200, "application/json", resp);
            }
        });

    // Bench-only OTA: reachable at /update over whichever interface is up
    // (SoftAP or STA), same as everything else this class serves. No auth —
    // trusted local network, physically-present developer. A field-facing
    // OTA path (Kamta's flasher app, over the internet-reachable STA link)
    // will need the short-lived server-issued token described in the NB
    // Agri Flasher plan before it ships; don't reuse this endpoint for that
    // without adding it.
    ElegantOTA.begin(&_server);
    // Required now that OTA is confirmed working over a customer's home WiFi
    // (Cloud mode), not just an isolated bench SoftAP — see the comment by
    // OTA_USERNAME/OTA_PASSWORD in Config.h for why this stopped being
    // optional. Must stay in sync with WifiOtaFlasher.kt on the app side.
    ElegantOTA.setAuth(OTA_USERNAME, OTA_PASSWORD);
    ElegantOTA.onStart([]() {
        Serial.println("[LocalServer] OTA update starting");
        // Update.write() erases flash in up to 64KB blocks (Updater.cpp)
        // directly inside the AsyncTCP task's own call stack — a slow-flash
        // erase can run long enough to starve that core's IDLE task past
        // the default ~5s task-watchdog timeout, aborting/rebooting
        // mid-transfer. Bench-verified: this is exactly the community-
        // documented failure (ESP32Async/AsyncTCP#107, ElegantOTA#47 —
        // same "task_wdt...Aborting" signature), not something specific to
        // this project. Disabling the idle-task watchdogs for the OTA
        // window (Arduino-ESP32's standard mitigation for a long blocking
        // call) is safe here: LEDs/relay/scheduler still run every loop()
        // tick regardless, and a stuck OTA now hangs instead of silently
        // rebooting into a half-written partition.
        disableCore0WDT();
        disableCore1WDT();
        // Flash erase also draws a brief current spike large enough to sag
        // a marginal supply below the brownout threshold (2.43V default) —
        // a very plausible match for the "board disappeared off USB
        // mid-transfer" seen in earlier bench testing: a brownout reset
        // power-cycles the whole board, including its USB-serial chip,
        // which looks from the host side exactly like a cable disconnect.
        // Widen the margin for just the OTA window rather than disabling
        // this safety net permanently.
        s_savedBrownoutReg = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
        s_lastOtaProgressMs = millis();
    });
    ElegantOTA.onProgress([](size_t current, size_t total) {
        s_lastOtaProgressMs = millis();
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 1000) {
            lastLog = millis();
            Serial.printf("[LocalServer] OTA progress: %u / %u bytes\n", current, total);
        }
    });
    ElegantOTA.onEnd([](bool success) {
        Serial.printf("[LocalServer] OTA update %s\n", success ? "succeeded — rebooting" : "failed");
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, s_savedBrownoutReg);
        enableCore0WDT();
        enableCore1WDT();
    });

    _server.begin();
    Serial.println("[LocalServer] HTTP+WS server started on port 80 (all interfaces)");
    Serial.println("[LocalServer] OTA update portal at /update");
}

void LocalServer::loop() {
    _ws.cleanupClients();
    ElegantOTA.loop();

    // See the OTA_STALL_TIMEOUT_MS comment above — a dead client otherwise
    // leaves this open indefinitely with watchdogs/brownout protection off.
    if (Update.isRunning() && millis() - s_lastOtaProgressMs > OTA_STALL_TIMEOUT_MS) {
        Serial.printf("[LocalServer] OTA stalled — no progress for %lus, aborting and restoring safety\n",
                      (unsigned long)(OTA_STALL_TIMEOUT_MS / 1000));
        Update.abort();
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, s_savedBrownoutReg);
        enableCore0WDT();
        enableCore1WDT();
    }
}

void LocalServer::closeAllClients() {
    if (_ws.count() > 0) {
        Serial.printf("[LocalServer] Closing %d WS client(s) — STA reconnected, "
                       "any local client at this point is stale\n", _ws.count());
    }
    _ws.closeAll();
}

bool LocalServer::isConnected()  { return _ws.count() > 0; }
int  LocalServer::stationCount() { return WiFi.softAPgetStationNum(); }

void LocalServer::_broadcast(const char* type, const String& json) {
    String msg = "{\"type\":\"" + String(type) + "\",\"data\":" + json + "}";
    _ws.textAll(msg);
}

void LocalServer::publishStatus(const String& json) {
    _lastStatus = json;
    _broadcast("status", json);
}
void LocalServer::publishActiveCycle(const String& json) {
    _lastActive = json;
    _broadcast("active_cycle", json);
}
void LocalServer::publishCycles(const String& json) {
    _lastCycles = json;
    _broadcast("cycles", json);
}
void LocalServer::publishWifiScanResult(const String& json) {
    _broadcast("wifi_scan_result", json);
}
bool LocalServer::publishHistory(const String& json) {
    _lastHistory = json;
    _broadcast("history", json);
    return true;
}

String LocalServer::_dispatch(const String& msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg) != DeserializationError::Ok) {
        return "{\"ok\":false,\"error\":\"invalid JSON\"}";
    }
    const char* cmd = doc["cmd"];
    if (!cmd) return "{\"ok\":false,\"error\":\"no cmd\"}";

    Serial.printf("[LocalServer] Command: %s\n", cmd);
    if (!onCommand) return "{\"ok\":false,\"error\":\"not ready\"}";

    String result = onCommand(String(cmd), doc.as<JsonObject>());
    // onCommand returns the raw data payload (e.g. "{}" or a cycles array);
    // wrap it consistently so the app can tell success from failure.
    return "{\"ok\":true,\"cmd\":\"" + String(cmd) + "\",\"data\":" + result + "}";
}
