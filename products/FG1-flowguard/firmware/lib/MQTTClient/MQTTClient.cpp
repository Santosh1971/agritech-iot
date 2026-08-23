#include "MQTTClient.h"

static MQTTHandler* _instance = nullptr;

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        String msg;
        for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
        Serial.printf("[MQTT] Received on %s: %s\n", topic, msg.c_str());
        JsonDocument doc;
        if (deserializeJson(doc, msg) != DeserializationError::Ok) return;
        const char* cmd = doc["cmd"];
        if (cmd && _instance->onCommand)
            _instance->onCommand(String(cmd), doc.as<JsonObject>());
    }
}

void MQTTHandler::begin(const char* broker, uint16_t port, const char* user, const char* pass,
                         const String& macSuffix) {
    _instance = this;
    strlcpy(_broker, broker, sizeof(_broker));
    _port = port;
    strlcpy(_user, user, sizeof(_user));
    strlcpy(_pass, pass, sizeof(_pass));

    // Topics now scoped per-device: "swc/SWC_001/<4-char MAC suffix>/...".
    // Client ID uses the same suffix and is STABLE across reconnects
    // (previously a fresh random hex value every single reconnect
    // attempt — harmless for routing, but confusing in the logs, and
    // not standard MQTT practice; a stable ID lets the broker track this
    // device's session properly instead of treating every reconnect as
    // an unrelated new client).
    _deviceId     = String(DEVICE_ID) + "_" + macSuffix;
    _topicStatus  = "agrisense/FG1/" + _deviceId + "/status";
    _topicHistory = "agrisense/FG1/" + _deviceId + "/history";
    _topicActive  = "agrisense/FG1/" + _deviceId + "/active_cycle";
    _topicCycles  = "agrisense/FG1/" + _deviceId + "/cycles";
    _topicCmd     = "agrisense/FG1/" + _deviceId + "/command";
    _topicLwt     = "agrisense/FG1/" + _deviceId + "/lwt";
    _topicCyclesConfig = "agrisense/FG1/" + _deviceId + "/cycles_config";

    _mqtt.setClient(_wifiClient);
    _mqtt.setServer(_broker, _port);
    _mqtt.setCallback(mqttCallback);
    _mqtt.setKeepAlive(30);
    _mqtt.setBufferSize(24576);  // room for a full month's history range payload (~150-200 entries)
    Serial.printf("[MQTT] Configured — %s:%d (client %s)\n", _broker, _port, _deviceId.c_str());
}

void MQTTHandler::loop() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!_mqtt.connected()) {
        uint32_t now = millis();
        if (now - _lastReconnectAttempt > 5000) {
            _lastReconnectAttempt = now;
            _reconnect();
        }
    } else {
        _mqtt.loop();
    }
}

void MQTTHandler::_reconnect() {
    Serial.printf("[MQTT] Connecting as %s...\n", _deviceId.c_str());
    bool ok = _mqtt.connect(
        _deviceId.c_str(), _user, _pass,
        _topicLwt.c_str(), 1, true, "{\"online\":false}"
    );
    if (ok) {
        Serial.println("[MQTT] Connected");
        _mqtt.publish(_topicLwt.c_str(), "{\"online\":true}", true);
        _mqtt.subscribe(_topicCmd.c_str());
        Serial.printf("[MQTT] Subscribed to %s\n", _topicCmd.c_str());
        _mqtt.subscribe(_topicCyclesConfig.c_str());
        Serial.printf("[MQTT] Subscribed to %s\n", _topicCyclesConfig.c_str());
    } else {
        Serial.printf("[MQTT] Failed rc=%d — retry in 5s\n", _mqtt.state());
    }
}

bool MQTTHandler::isConnected() { return _mqtt.connected(); }

void MQTTHandler::publishStatus(const String& json) {
    _mqtt.publish(_topicStatus.c_str(), json.c_str(), true);
}
void MQTTHandler::publishActiveCycle(const String& json) {
    _mqtt.publish(_topicActive.c_str(), json.c_str(), true);
}
void MQTTHandler::publishCycles(const String& json) {
    _mqtt.publish(_topicCycles.c_str(), json.c_str(), true);
}
bool MQTTHandler::publishHistory(const String& json) {
    // A single publish() failure isn't necessarily a dead connection —
    // seen in testing to fail transiently even in clear steady-state
    // operation (no reconnect anywhere nearby), so a short retry is a
    // cheap, pragmatic mitigation regardless of the exact underlying
    // cause. 3 attempts, 100ms apart.
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (_mqtt.publish(_topicHistory.c_str(), json.c_str(), false)) return true;
        Serial.printf("[MQTT] publishHistory attempt %d/3 failed (state=%d)\n",
                      attempt, _mqtt.state());
        if (attempt < 3) delay(100);
    }
    return false;
}

int MQTTHandler::state() { return _mqtt.state(); }
