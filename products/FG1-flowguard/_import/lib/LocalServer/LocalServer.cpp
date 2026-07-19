#include "LocalServer.h"

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

    _server.begin();
    Serial.println("[LocalServer] HTTP+WS server started on port 80 (all interfaces)");
}

void LocalServer::loop() {
    _ws.cleanupClients();
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
