#pragma once
// Controller side of the sensor-node network: discovery, pairing, readings,
// link quality, and the control T/RH (one node, average of chosen nodes, or
// all). The controller's own sensor (in the pump panel, shaded) is the
// "Outside" reference and is only used for control if chosen — or as the
// fallback while no node is chosen at all (first commissioning).
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "EspNowProto.h"
#include "EspNowLink.h"
#include "MistLogic.h"

struct ControlReading {
    bool  ok = false;
    float t = 0, rh = 0;
    uint8_t used = 0;        // sources that went into the average
    uint8_t selected = 0;    // sources chosen in the app (0 = fallback to Outside)
    bool  fallback = false;  // no source chosen -> using the controller's own sensor
};

class NodeManager {
public:
    void begin(Preferences* prefs, const String& ctrlId);
    void loop();                                  // handle incoming ESP-NOW packets

    // Controller's own sensor ("outside" reference).
    void updateLocal(bool ok, float t, float rh, uint32_t nowSec);
    bool localOk(uint32_t nowSec) const;
    float localT(uint32_t nowSec) const  { return _localT.value(nowSec); }
    float localRh(uint32_t nowSec) const { return _localRh.value(nowSec); }

    ControlReading control(uint32_t nowSec) const;

    // App actions — return false + error text when not possible.
    bool add(const String& mac, String& err);
    bool remove(const String& mac, String& err);
    bool rename(const String& mac, const String& name, String& err);
    bool identify(const String& mac, String& err);
    // sources: node MACs and/or "local". Empty = fallback to local.
    void setControlSources(JsonArrayConst sources);

    void toJson(JsonObject root, uint32_t nowSec) const;
    uint8_t count() const;
    uint8_t onlineCount(uint32_t nowSec) const;

    // Set by NodeManager for main.cpp's event log.
    std::function<void(const String& msg)> onEvent;
    std::function<void()> onData;             // every reading received (LED flash)

    uint32_t sensorTimeoutSec = 120;

private:
    struct Node {
        bool     used = false;
        uint8_t  mac[6] = {0};
        char     name[20] = {0};
        bool     control = false;
        // live (not persisted)
        uint32_t lastSec = 0;
        bool     everSeen = false;
        bool     sensorOk = false;
        uint8_t  sensorKind = 0;
        float    t = 0, rh = 0;
        int8_t   rssi = 0, rssiAtNode = 0;
        uint16_t lastSeq = 0;
        uint32_t rx = 0, lost = 0;
        uint32_t uptime = 0;
        char     fw[12] = {0};
        bool     resetByUser = false;     // node said it is no longer paired
        mist::RollingAvg avgT, avgRh;
    };
    struct Disc {
        bool     used = false;
        uint8_t  mac[6] = {0};
        uint32_t lastMs = 0;
        int8_t   rssi = 0;
        char     fw[12] = {0};
        bool     pending = false;
    };
    struct Persist { uint8_t mac[6]; char name[20]; uint8_t control; };

    void _handle(const EspNowRx& rx);
    void _onHello(const EspNowRx& rx, const PktHello& p);
    void _onData(const EspNowRx& rx, const PktData& p);
    void _finishPair(const uint8_t* mac, const char* fw);
    void _sendAnnounce(const uint8_t* mac, bool knows);
    void _sendPair(const uint8_t* mac);
    void _sendSimple(const uint8_t* mac, PktType t);
    Node* _find(const uint8_t* mac);
    Disc* _findDisc(const uint8_t* mac);
    Disc* _discSlot(const uint8_t* mac);
    void _save();
    void _event(const String& s) { if (onEvent) onEvent(s); }
    static String _shortId(const uint8_t* mac);

    Preferences* _prefs = nullptr;
    char  _ctrlId[12] = {0};
    Node  _nodes[MAX_NODES];
    Disc  _disc[MAX_DISCOVERED];
    bool  _localControl = false;
    bool  _localSensorOk = false;
    mist::RollingAvg _localT, _localRh;
};
