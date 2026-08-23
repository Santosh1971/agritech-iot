#pragma once
#include "Config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"

class MQTTHandler {
public:
    // broker/user/pass are copied internally, so caller's buffers can go out of scope after this call.
    // macSuffix (last 4 chars of the device's MAC-derived ID) scopes the
    // MQTT client ID and every topic to THIS device specifically — e.g.
    // "swc/SWC_001/9E74/status". Previously topics were shared across
    // every physical device ("swc/SWC_001/status" for all of them),
    // which meant with more than one device in Cloud mode simultaneously,
    // a command meant for one would go to all of them, and their status
    // updates would overwrite each other on the same retained topic.
    void begin(const char* broker, uint16_t port, const char* user, const char* pass,
               const String& macSuffix);
    void loop();
    bool isConnected();

    void publishStatus(const String& json);
    void publishActiveCycle(const String& json);
    void publishCycles(const String& json);
    bool publishHistory(const String& json);

    // Raw PubSubClient connection-state code, for diagnosing a publish
    // failure that isn't accompanied by any visible reconnect (state() is
    // more specific than just connected()/not — e.g. distinguishes a
    // clean disconnect from a timeout from a protocol-level rejection).
    int state();

    // Command callbacks — wired from main.cpp
    std::function<void(const String& cmd, const JsonObject& payload)> onCommand;

private:
    void _reconnect();
    void _callback(char* topic, byte* payload, unsigned int length);

    WiFiClient   _wifiClient;
    PubSubClient _mqtt;
    uint32_t     _lastReconnectAttempt = 0;

    char     _broker[128] = {0};
    uint16_t _port        = 1883;
    char     _user[64]    = {0};
    char     _pass[64]    = {0};

    String _deviceId;
    // _topicCyclesConfig is a separate, retained-message channel for
    // pushing cycle-schedule updates specifically (distinct from
    // _topicCmd, which stays non-retained/ephemeral — correct for
    // one-shot actions like manual_on, but wrong for a schedule update:
    // if the device is offline when the app saves new cycles, a
    // non-retained cmd message is simply lost forever. A retained
    // message on its own topic is held by the broker and delivered the
    // instant the device reconnects and subscribes, exactly like the
    // existing retained status/cycles/lwt topics.
    String _topicStatus, _topicHistory, _topicActive, _topicCycles, _topicCmd, _topicLwt, _topicCyclesConfig;
};
