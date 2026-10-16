#include "MQTTClient.h"

static MQTTHandler* _instance = nullptr;

// Queue sizes: outgoing holds a burst of status + active_cycle + cycles +
// history; incoming holds a few back-to-back commands (e.g. the retained
// cycles_config delivered right after a subscribe plus a manual command).
static const UBaseType_t OUT_QUEUE_LEN = 6;
static const UBaseType_t IN_QUEUE_LEN  = 4;

static const uint32_t RECONNECT_MIN_MS = 5000;
static const uint32_t RECONNECT_MAX_MS = 60000;

static const uint32_t MQTT_TASK_STACK = 8192;
static const UBaseType_t MQTT_TASK_PRIO = 1;
// Core 1, same priority as Arduino's loopTask — NOT core 0. PubSubClient
// busy-waits (yield() only, which never lets the priority-0 idle task in)
// for up to its 15s socket timeout while waiting for CONNACK. On core 0
// that starves IDLE0, and the task watchdog (5s, panic=y, watches only
// IDLE0) reboots the board — bench-seen 2026-10-16 on a slow broker
// reply. On core 1 IDLE1 isn't watched, and equal-priority time slicing
// keeps loop() running alongside a busy-wait.
static const BaseType_t  MQTT_TASK_CORE = 1;

// Runs in the MQTT task (inside _mqtt.loop()). Only copies the payload —
// parsing and dispatch happen on the Arduino loop task, see loop().
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!_instance) return;
    Serial.printf("[MQTT] Received on %s (%u bytes)\n", topic, length);
    _instance->_enqueueIncoming(payload, length);
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

    _outQ = xQueueCreate(OUT_QUEUE_LEN, sizeof(OutMsg));
    _inQ  = xQueueCreate(IN_QUEUE_LEN,  sizeof(char*));
    xTaskCreatePinnedToCore(_taskEntry, "mqtt", MQTT_TASK_STACK, this,
                            MQTT_TASK_PRIO, nullptr, MQTT_TASK_CORE);
    Serial.printf("[MQTT] Configured — %s:%d (client %s)\n", _broker, _port, _deviceId.c_str());
}

// ---------------------------------------------------------------------------
// Arduino loop task side
// ---------------------------------------------------------------------------

void MQTTHandler::loop() {
    if (!_inQ) return;
    char* msg = nullptr;
    while (xQueueReceive(_inQ, &msg, 0) == pdTRUE) {
        Serial.printf("[MQTT] Command: %s\n", msg);
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, msg);
        free(msg);
        if (err != DeserializationError::Ok) continue;
        const char* cmd = doc["cmd"];
        if (cmd && onCommand) onCommand(String(cmd), doc.as<JsonObject>());
    }
}

bool MQTTHandler::isConnected() { return _connected; }
int  MQTTHandler::state()       { return _state; }

bool MQTTHandler::_enqueueOutgoing(OutTopic topic, const String& json) {
    // Not connected: drop, as the old direct publish() would have failed.
    // Queuing it instead would deliver stale status after a reconnect.
    if (!_outQ || !_connected) return false;
    OutMsg m{topic, strdup(json.c_str())};
    if (!m.payload) {
        Serial.println("[MQTT] Out of memory queuing publish — dropped");
        return false;
    }
    if (xQueueSend(_outQ, &m, 0) != pdTRUE) {
        Serial.printf("[MQTT] Publish queue full — dropped topic %d\n", (int)topic);
        free(m.payload);
        return false;
    }
    return true;
}

void MQTTHandler::publishStatus(const String& json)      { _enqueueOutgoing(OUT_STATUS, json); }
void MQTTHandler::publishActiveCycle(const String& json) { _enqueueOutgoing(OUT_ACTIVE, json); }
void MQTTHandler::publishCycles(const String& json)      { _enqueueOutgoing(OUT_CYCLES, json); }
bool MQTTHandler::publishHistory(const String& json)     { return _enqueueOutgoing(OUT_HISTORY, json); }

// ---------------------------------------------------------------------------
// MQTT task side — the only code that touches _mqtt / _wifiClient
// ---------------------------------------------------------------------------

void MQTTHandler::_enqueueIncoming(const byte* payload, unsigned int length) {
    char* msg = (char*)malloc(length + 1);
    if (!msg) {
        Serial.println("[MQTT] Out of memory queuing command — dropped");
        return;
    }
    memcpy(msg, payload, length);
    msg[length] = '\0';
    if (xQueueSend(_inQ, &msg, 0) != pdTRUE) {
        Serial.println("[MQTT] Command queue full — dropped");
        free(msg);
    }
}

void MQTTHandler::_taskEntry(void* arg) {
    static_cast<MQTTHandler*>(arg)->_taskLoop();
}

void MQTTHandler::_taskLoop() {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            if (_connected) {
                _mqtt.disconnect();
                _connected = false;
                _state     = _mqtt.state();
            }
            _drainOutgoing();   // nothing can be sent; discard
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!_mqtt.connected()) {
            _connected = false;
            _state     = _mqtt.state();
            if (millis() - _lastReconnectAttempt >= _reconnectBackoffMs) {
                _reconnect();
                // Stamp AFTER the attempt: a failed DNS/TCP connect can take
                // several seconds, and stamping before it meant the next
                // "retry in 5s" fired immediately after a slow failure.
                _lastReconnectAttempt = millis();
            }
        } else {
            _mqtt.loop();
        }

        _drainOutgoing();
        vTaskDelay(pdMS_TO_TICKS(10));
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
        _reconnectBackoffMs = RECONNECT_MIN_MS;
        _connected = true;
    } else {
        // Back off 5s -> 10s -> 20s -> 40s -> 60s cap while the broker is
        // unreachable (e.g. router up, internet down), so a dead uplink
        // doesn't keep the radio busy with back-to-back DNS/TCP attempts.
        _reconnectBackoffMs = _reconnectBackoffMs == 0 ? RECONNECT_MIN_MS
                            : min(_reconnectBackoffMs * 2, RECONNECT_MAX_MS);
        Serial.printf("[MQTT] Failed rc=%d — retry in %lus\n",
                      _mqtt.state(), (unsigned long)(_reconnectBackoffMs / 1000));
    }
    _state = _mqtt.state();
}

void MQTTHandler::_drainOutgoing() {
    OutMsg m;
    while (xQueueReceive(_outQ, &m, 0) == pdTRUE) {
        if (_connected && _mqtt.connected()) {
            switch (m.topic) {
                case OUT_STATUS:  _mqtt.publish(_topicStatus.c_str(), m.payload, true); break;
                case OUT_ACTIVE:  _mqtt.publish(_topicActive.c_str(), m.payload, true); break;
                case OUT_CYCLES:  _mqtt.publish(_topicCycles.c_str(), m.payload, true); break;
                case OUT_HISTORY:
                    // A single publish() failure isn't necessarily a dead connection —
                    // seen in testing to fail transiently even in clear steady-state
                    // operation (no reconnect anywhere nearby), so a short retry is a
                    // cheap, pragmatic mitigation regardless of the exact underlying
                    // cause. 3 attempts, 100ms apart. (Safe to delay here: this is
                    // the MQTT task, not loop().)
                    for (int attempt = 1; attempt <= 3; attempt++) {
                        if (_mqtt.publish(_topicHistory.c_str(), m.payload, false)) break;
                        Serial.printf("[MQTT] publishHistory attempt %d/3 failed (%u bytes, state=%d)\n",
                                      attempt, (unsigned)strlen(m.payload), _mqtt.state());
                        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    break;
            }
        }
        free(m.payload);
    }
}
