#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------------
// Cloud link: WiFi STA (farm router) + MQTT to the shared AgriSense broker.
//
// Design rule: NOTHING here may ever stall the LoRa loop. DNS lookup, TCP
// connect and MQTT keep-alive can all block for seconds, and the Pump's
// fail-safe is 60s, so all of it runs in its own FreeRTOS task. The two
// sides only meet through:
//   - a command queue   (cloud task -> main loop): raw JSON strings, which
//                        the main loop executes with the SAME handlers the
//                        local HTTP API uses
//   - a status string   (main loop -> cloud task): latest snapshot, copied
//                        under a mutex, published retained
// so pumps[] / NVS / the radio are only ever touched from the main loop.
//
// Topics mirror WM1/FG1:   agrisense/WPC/WPC_<masterId8>/{status,command,lwt}
// Credentials are the shared FG1 pair (see below); hardening is a later step.
// ---------------------------------------------------------------------

#define CLOUD_BROKER_HOST "mqtt.agrisenseandcontrol.in"
#define CLOUD_BROKER_PORT 1883
// Same per-product credential FG1 uses: the broker already accepts it for agrisense/WPC/...
// (verified: connect, subscribe and a publish round trip). A dedicated wpc-device user was
// tried first and does not exist on the broker.
#define CLOUD_MQTT_USER   "fg1-device"
#define CLOUD_MQTT_PASS   "asacfg1"

#define CLOUD_STATUS_MIN_INTERVAL_MS 1000UL    // never publish faster than this
#define CLOUD_STATUS_MAX_INTERVAL_MS 10000UL   // republish at least this often
#define CLOUD_WIFI_RETRY_MS          30000UL   // STA scans disturb the SoftAP, so retry sparingly
#define CLOUD_MQTT_RETRY_MS          5000UL
#define CLOUD_CMD_QUEUE_LEN          8
#define CLOUD_MAX_PAYLOAD            1024

class CloudLink {
public:
  void begin(const String& deviceId) {
    _base = "agrisense/WPC/" + deviceId + "/";
    _deviceId = deviceId;
    _topicStatus  = _base + "status";
    _topicCommand = _base + "command";
    _topicLwt     = _base + "lwt";

    _statusMutex = xSemaphoreCreateMutex();
    _cmdQueue = xQueueCreate(CLOUD_CMD_QUEUE_LEN, sizeof(char*));

    _mqtt.setClient(_wifiClient);
    _mqtt.setServer(CLOUD_BROKER_HOST, CLOUD_BROKER_PORT);
    _mqtt.setBufferSize(8192);   // 20 pumps of status JSON is ~5KB
    _mqtt.setKeepAlive(30);
    _mqtt.setSocketTimeout(5);
    _mqtt.setCallback([this](char* topic, uint8_t* payload, unsigned int len) {
      onMessage(payload, len);
    });

    WiFi.setAutoReconnect(true);
    xTaskCreatePinnedToCore(taskEntry, "cloud", 8192, this, 1, nullptr, 0);
  }

  // Store credentials for the task to use; an empty SSID disconnects and
  // stops trying. Persisting them is the caller's job (NVS).
  void setWifi(const String& ssid, const String& pass) {
    _ssid = ssid;   // main-loop-side copy, only used by ssid()/wifiConfigured()
    xSemaphoreTake(_statusMutex, portMAX_DELAY);
    _pendSsid = ssid;
    _pendPass = pass;
    _wifiRequested = true;
    xSemaphoreGive(_statusMutex);
  }

  bool wifiConfigured() const { return _ssid.length() > 0; }
  bool wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool mqttConnected() { return _mqttUp; }
  String ssid() const { return _ssid; }
  String ip() const { return wifiConnected() ? WiFi.localIP().toString() : String(""); }

  // Main loop -> cloud task. Cheap copy under a mutex.
  void setStatus(const String& json) {
    if (xSemaphoreTake(_statusMutex, 10) == pdTRUE) {
      _status = json;
      _statusSeq++;
      xSemaphoreGive(_statusMutex);
    }
  }

  // Main loop: fetch one pending command, if any. Caller owns the String.
  bool popCommand(String& out) {
    char* p = nullptr;
    if (xQueueReceive(_cmdQueue, &p, 0) != pdTRUE || !p) return false;
    out = String(p);
    free(p);
    return true;
  }

private:
  static void taskEntry(void* arg) { static_cast<CloudLink*>(arg)->run(); }

  void onMessage(uint8_t* payload, unsigned int len) {
    if (len == 0 || len > CLOUD_MAX_PAYLOAD) return;
    char* copy = (char*)malloc(len + 1);
    if (!copy) return;
    memcpy(copy, payload, len);
    copy[len] = '\0';
    if (xQueueSend(_cmdQueue, &copy, 0) != pdTRUE) free(copy);   // queue full: drop, app can resend
  }

  void run() {
    String ssid, pass;   // task-local copies, never shared
    uint32_t lastWifiBegin = 0;
    uint32_t lastMqttTry = 0;
    uint32_t lastPublish = 0;
    uint32_t publishedSeq = 0;
    bool wasConnected = false;

    for (;;) {
      if (_wifiRequested) {
        xSemaphoreTake(_statusMutex, portMAX_DELAY);
        ssid = _pendSsid;
        pass = _pendPass;
        _wifiRequested = false;
        xSemaphoreGive(_statusMutex);
        WiFi.disconnect(false, false);   // keep the AP up, drop only the STA side
        if (ssid.length()) {
          WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);   // open network = no password
          lastWifiBegin = millis();
        }
      }

      if (ssid.length() && WiFi.status() != WL_CONNECTED &&
          millis() - lastWifiBegin > CLOUD_WIFI_RETRY_MS) {
        WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);   // open network = no password
        lastWifiBegin = millis();
      }

      bool up = (WiFi.status() == WL_CONNECTED);
      if (up != wasConnected) {
        wasConnected = up;
        Serial.println(up ? F("[CLOUD] WiFi STA connected") : F("[CLOUD] WiFi STA lost"));
      }

      if (up) {
        if (!_mqtt.connected()) {
          _mqttUp = false;
          if (millis() - lastMqttTry > CLOUD_MQTT_RETRY_MS) {
            lastMqttTry = millis();
            connectMqtt();
          }
        } else {
          _mqtt.loop();
          publishIfDue(lastPublish, publishedSeq);
        }
      } else {
        _mqttUp = false;
      }
      vTaskDelay(pdMS_TO_TICKS(25));
    }
  }

  void connectMqtt() {
    String clientId = "wpc_" + _deviceId + "_" + String((uint32_t)(esp_random() & 0xFFFF), HEX);
    bool ok = _mqtt.connect(clientId.c_str(), CLOUD_MQTT_USER, CLOUD_MQTT_PASS,
                            _topicLwt.c_str(), 1, true, "{\"online\":false}");
    if (!ok) {
      Serial.printf("[CLOUD] MQTT connect failed, state=%d\n", _mqtt.state());
      return;
    }
    Serial.println(F("[CLOUD] MQTT connected"));
    _mqtt.subscribe(_topicCommand.c_str());
    _mqtt.publish(_topicLwt.c_str(), "{\"online\":true}", true);
    _mqttUp = true;
    _forcePublish = true;
  }

  void publishIfDue(uint32_t& lastPublish, uint32_t& publishedSeq) {
    uint32_t now = millis();
    if (now - lastPublish < CLOUD_STATUS_MIN_INTERVAL_MS) return;

    String snap;
    uint32_t seq;
    if (xSemaphoreTake(_statusMutex, 10) != pdTRUE) return;
    snap = _status;
    seq = _statusSeq;
    xSemaphoreGive(_statusMutex);
    if (snap.length() == 0) return;

    bool changed = (seq != publishedSeq);
    bool stale = (now - lastPublish >= CLOUD_STATUS_MAX_INTERVAL_MS);
    if (!changed && !stale && !_forcePublish) return;

    if (_mqtt.publish(_topicStatus.c_str(), snap.c_str(), true)) {
      lastPublish = now;
      publishedSeq = seq;
      _forcePublish = false;
    }
  }

  WiFiClient _wifiClient;
  PubSubClient _mqtt;
  String _base, _deviceId, _topicStatus, _topicCommand, _topicLwt;
  String _ssid, _pass;
  String _pendSsid, _pendPass;   // guarded by _statusMutex
  String _status;
  uint32_t _statusSeq = 0;
  SemaphoreHandle_t _statusMutex = nullptr;
  QueueHandle_t _cmdQueue = nullptr;
  volatile bool _wifiRequested = false;
  volatile bool _mqttUp = false;
  volatile bool _forcePublish = false;
};
