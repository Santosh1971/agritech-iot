#include "Cloud.h"

static Cloud* s_cloud = nullptr;

void Cloud::begin(const String& deviceId) {
    s_cloud = this;
    _id = deviceId;
    String base = String(MQTT_TOPIC_ROOT) + deviceId + "/";
    _tStatus = base + "status";
    _tResp = base + "resp";
    _tLwt = base + "lwt";
    _tCmd = base + "command";
    _tSettings = base + "settings";

    _mqtt.setClient(_wifi);
    _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    _mqtt.setKeepAlive(30);
    _mqtt.setBufferSize(16384);                 // 24 h trend reply is ~9 KB
    _mqtt.setCallback([](char* topic, byte* p, unsigned int len) {
        if (s_cloud) s_cloud->_incoming(p, len);
    });
    _outQ = xQueueCreate(6, sizeof(Out));
    _inQ = xQueueCreate(6, sizeof(char*));
    xTaskCreatePinnedToCore(_taskEntry, "mqtt", 8192, this, 1, nullptr, 1);
    Serial.printf("[MQTT] %s, topics %s*\n", MQTT_BROKER, base.c_str());
}

void Cloud::_incoming(const byte* p, unsigned len) {
    char* m = (char*)malloc(len + 1);
    if (!m) return;
    memcpy(m, p, len);
    m[len] = 0;
    if (xQueueSend(_inQ, &m, 0) != pdTRUE) free(m);
}

void Cloud::loop() {
    if (!_inQ) return;
    char* m;
    while (xQueueReceive(_inQ, &m, 0) == pdTRUE) {
        JsonDocument doc;
        bool ok = !deserializeJson(doc, m);
        free(m);
        if (!ok) continue;
        const char* cmd = doc["cmd"];
        if (cmd && onCommand) onCommand(String(cmd), doc.as<JsonObject>());
    }
}

bool Cloud::_enqueue(uint8_t topic, const String& json) {
    if (!_outQ || !_connected) return false;
    Out o{topic, strdup(json.c_str())};
    if (!o.payload) return false;
    if (xQueueSend(_outQ, &o, 0) != pdTRUE) { free(o.payload); return false; }
    return true;
}

void Cloud::_task() {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            if (_connected) { _mqtt.disconnect(); _connected = false; }
            _drain();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!_mqtt.connected()) {
            _connected = false;
            if (millis() - _lastTry >= _backoff) { _reconnect(); _lastTry = millis(); }
        } else {
            _mqtt.loop();
        }
        _drain();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Cloud::_reconnect() {
    if (_mqtt.connect(_id.c_str(), MQTT_USER, MQTT_PASS, _tLwt.c_str(), 1, true, "{\"online\":false}")) {
        _mqtt.publish(_tLwt.c_str(), "{\"online\":true}", true);
        _mqtt.subscribe(_tCmd.c_str());
        _mqtt.subscribe(_tSettings.c_str());
        _backoff = 5000;
        _connected = true;
        Serial.println("[MQTT] Connected");
    } else {
        _backoff = _backoff == 0 ? 5000 : min<uint32_t>(_backoff * 2, 60000);
        Serial.printf("[MQTT] Failed rc=%d, retry in %lus\n", _mqtt.state(), (unsigned long)(_backoff / 1000));
    }
}

void Cloud::_drain() {
    Out o;
    while (xQueueReceive(_outQ, &o, 0) == pdTRUE) {
        if (_connected && _mqtt.connected()) {
            if (o.topic == 0) _mqtt.publish(_tStatus.c_str(), o.payload, true);
            else              _mqtt.publish(_tResp.c_str(), o.payload, false);
        }
        free(o.payload);
    }
}
