#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include "Config.h"
#include "DeviceIdentity.h"
#include "CommandHandler.h"

// Topics mirror FG1's actual mqtt_service.dart pattern:
//   agrisense/WM1/<deviceId>/status           (device -> cloud, retained)
//   agrisense/WM1/<deviceId>/programs         (device -> cloud) — full list, pushed after any change
//   agrisense/WM1/<deviceId>/library          (device -> cloud) — full sequence library, pushed after any change
//   agrisense/WM1/<deviceId>/command          (cloud -> device) — {"cmd": ...}
//   agrisense/WM1/<deviceId>/programs_config  (cloud -> device, RETAINED) — {"cmd":"set_programs",...}
//   agrisense/WM1/<deviceId>/library_config   (cloud -> device, RETAINED) — {"cmd":"set_sequence_library",...}
//   agrisense/WM1/<deviceId>/lwt              (device -> cloud, retained) — {"online": bool}
//   agrisense/WM1/<deviceId>/history          (device -> cloud) — get_history's reply, published
//                                              here since there's no periodic history broadcast
//                                              the way there is for status/programs/library.
//
// programs_config is retained (same reasoning as FG1's cycles_config):
// only needs a live link to the BROKER, not the device — if the device
// is offline when the app saves a program, the broker holds the
// message and delivers it the instant the device reconnects.
class MqttClientWrapper {
public:
  void begin(CommandHandler* handler) {
    _handler = handler;
    _client.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    // Same buffer FG1 uses for the same reason — a history reply is the
    // one payload here that can genuinely be large (a query over many
    // records), unlike the small per-command acks everything else sends.
    _client.setBufferSize(24576);
    _client.setCallback([this](char* topic, uint8_t* payload, unsigned int len) {
      _onMessage(topic, payload, len);
    });

    String id = computeDeviceId();
    String base = "agrisense/WM1/" + id + "/";
    _topicStatus = base + "status";
    _topicPrograms = base + "programs";
    _topicLibrary = base + "library";
    _topicCommand = base + "command";
    _topicProgramsConfig = base + "programs_config";
    _topicLibraryConfig = base + "library_config";
    _topicLwt = base + "lwt";
    _topicHistory = base + "history";
  }

  void loop(bool wifiConnected) {
    if (!wifiConnected) return;
    if (!_client.connected()) {
      _tryConnect();
    } else {
      _client.loop();
    }
  }

  bool isConnected() { return _client.connected(); }

  void publishStatus(const String& json) {
    if (_client.connected()) _client.publish(_topicStatus.c_str(), json.c_str(), true);
  }
  void publishPrograms(const String& json) {
    if (_client.connected()) _client.publish(_topicPrograms.c_str(), json.c_str(), false);
  }
  void publishLibrary(const String& json) {
    if (_client.connected()) _client.publish(_topicLibrary.c_str(), json.c_str(), false);
  }

private:
  void _tryConnect() {
    uint32_t now = millis();
    if (now - _lastAttempt < RECONNECT_INTERVAL_MS) return;
    _lastAttempt = now;

    String id = computeDeviceId();
    String willPayload = "{\"online\":false}";
    bool ok = _client.connect(id.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
                               _topicLwt.c_str(), 1, true, willPayload.c_str());

    if (ok) {
      Serial.println("[MQTT] Connected");
      _client.subscribe(_topicCommand.c_str());
      _client.subscribe(_topicProgramsConfig.c_str());
      _client.subscribe(_topicLibraryConfig.c_str());
      _client.publish(_topicLwt.c_str(), "{\"online\":true}", true);
    } else {
      Serial.printf("[MQTT] Connect failed, state=%d\n", _client.state());
    }
  }

  void _onMessage(char* topic, uint8_t* payload, unsigned int len) {
    String msg((char*)payload, len);
    if (!_handler) return;
    // Both command and programs_config carry a {"cmd": ...} payload —
    // same handler either way, no routing needed beyond having
    // subscribed to both. Most replies aren't published anywhere for
    // MQTT (matches FG1: command acks aren't surfaced, the next status/
    // programs broadcast is the source of truth for what happened) —
    // get_history is the one exception, since there's no broadcast
    // equivalent for it the app could otherwise wait on. Bug fix: this
    // used to discard every reply unconditionally, silently dropping
    // get_history's result over Cloud mode entirely.
    _handler->handle(msg, [this](const String& reply) {
      if (reply.indexOf("\"cmd\":\"get_history\"") >= 0) {
        _client.publish(_topicHistory.c_str(), reply.c_str(), false);
      }
    });
  }

  static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

  WiFiClient _wifiClient;
  PubSubClient _client{_wifiClient};
  CommandHandler* _handler = nullptr;
  String _topicStatus, _topicPrograms, _topicLibrary, _topicCommand, _topicProgramsConfig, _topicLibraryConfig, _topicLwt, _topicHistory;
  uint32_t _lastAttempt = 0;
};
