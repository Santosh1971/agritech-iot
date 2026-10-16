#pragma once
#include "Config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"

// All network I/O (DNS, TCP connect, PubSubClient::loop, publish) runs in
// a dedicated FreeRTOS task, never in Arduino's loop(). Bench-seen
// 2026-10-16: a reconnect whose DNS lookup failed blocked loop() for
// 6.6s, freezing the LEDs and, worse, relay/scheduler servicing; with
// router up but internet down that repeats on every retry.
//
// Threading contract:
//  - PubSubClient/_wifiClient are touched ONLY by the MQTT task.
//  - publishX() copies the payload into a queue and returns at once.
//  - Received commands are queued by the task and dispatched to
//    onCommand from loop() — so handleCommand() (relay, scheduler, NVS)
//    still only ever runs on the Arduino loop task for MQTT commands.
//  - isConnected()/state() read flags the task keeps current.
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
    // Call from Arduino loop(): dispatches queued incoming commands. Never blocks.
    void loop();
    bool isConnected();

    // Non-blocking: queue the message for the MQTT task. Dropped (with a
    // log line) if not connected or the queue is full — same outcome as
    // the old direct publish() failing while offline.
    void publishStatus(const String& json);
    void publishActiveCycle(const String& json);
    void publishCycles(const String& json);
    // Returns true if queued; the task still retries the send 3x itself.
    bool publishHistory(const String& json);

    // Raw PubSubClient connection-state code, for diagnosing a publish
    // failure that isn't accompanied by any visible reconnect (state() is
    // more specific than just connected()/not — e.g. distinguishes a
    // clean disconnect from a timeout from a protocol-level rejection).
    int state();

    // Command callbacks — wired from main.cpp
    std::function<void(const String& cmd, const JsonObject& payload)> onCommand;

    // Called by the PubSubClient callback (MQTT task context) — queues the raw payload.
    void _enqueueIncoming(const byte* payload, unsigned int length);

private:
    enum OutTopic : uint8_t { OUT_STATUS, OUT_ACTIVE, OUT_CYCLES, OUT_HISTORY };
    struct OutMsg { OutTopic topic; char* payload; };   // payload malloc'd, freed by consumer

    static void _taskEntry(void* arg);
    void _taskLoop();
    void _reconnect();
    void _drainOutgoing();
    bool _enqueueOutgoing(OutTopic topic, const String& json);

    WiFiClient   _wifiClient;
    PubSubClient _mqtt;

    QueueHandle_t _outQ = nullptr;   // OutMsg
    QueueHandle_t _inQ  = nullptr;   // char* (malloc'd JSON payload)
    volatile bool _connected = false;
    volatile int  _state     = MQTT_DISCONNECTED;

    uint32_t _lastReconnectAttempt = 0;
    uint32_t _reconnectBackoffMs   = 0;   // 0 = try immediately

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
