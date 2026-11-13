#pragma once
// ESP-NOW packets between an FG1 Greenhouse TH controller and its sensor nodes.
// Shared by both builds — bump PROTO_VER on any layout change.
//
// Pairing (plug-and-play):
//   1. An unpaired node sweeps WiFi channels 1..13, broadcasting HELLO and
//      listening ~200 ms on each. Every controller that hears it replies
//      ANNOUNCE and lists the node as "discovered" in the app.
//   2. The user taps Add in the app. The controller marks the node pending
//      and answers its next HELLO with PAIR (the node is listening on that
//      channel right then). The node stores the controller MAC + channel,
//      replies PAIR_OK, and from then on sends DATA every 10 s.
//   3. If DATA stops being acknowledged (controller moved channel because
//      its router did, or rebooted into SoftAP), the node sweeps again with
//      HELLO(paired=1) and locks onto the channel its controller answers on.
//   4. Remove in the app -> UNPAIR; the node goes back to step 1.
//   A controller that gets DATA/HELLO from a node it does not know (e.g.
//   after a factory reset) answers UNPAIR, so the node never stays orphaned.
//
// Repeater version (builds with FG1_REPEATER; see docs/repeater-plan.md):
//   PROTO_VER stays 1 and only types 8+ are added, which the star firmware
//   ignores, so star and repeater builds work together. At most one hop:
//   far node -> repeater node -> controller. A repeater is a paired node that
//   talks to the controller directly.
//   - Far node HELLO: a repeater forwards an unpaired node's HELLO in
//     RELAY_UP (so the app lists it "via ..."), and answers a paired node's
//     HELLO (same controller) with RELAY_OFFER. The node picks direct or the
//     best offer (RELAY_JOIN) at the end of the channel dwell.
//   - Pairing through a repeater: controller -> RELAY_DOWN(PAIR) -> repeater
//     -> RELAY_PAIR (carries the controller's MAC) -> far node.
//   - Readings: far node DATA -> repeater -> RELAY_UP(DATA) -> controller,
//     DATA_ACK back the same way. Repeater-build nodes count a reading as
//     delivered by DATA_ACK once their controller has sent one (a star
//     controller never does, so they fall back to the radio's own ACK).
#include <stdint.h>

#define PROTO_MAGIC0 'F'
#define PROTO_MAGIC1 'P'
#define PROTO_VER    1

enum PktType : uint8_t {
    PKT_HELLO    = 1,   // node -> broadcast
    PKT_ANNOUNCE = 2,   // controller -> node (reply to HELLO)
    PKT_PAIR     = 3,   // controller -> node
    PKT_PAIR_OK  = 4,   // node -> controller
    PKT_UNPAIR   = 5,   // controller -> node
    PKT_DATA     = 6,   // node -> controller
    PKT_IDENTIFY = 7,   // controller -> node: blink LEDs for 15 s
    // ---- repeater version (FG1_REPEATER) ----
    PKT_RELAY_OFFER = 8,   // repeater -> far node: "I can carry you to your controller"
    PKT_RELAY_JOIN  = 9,   // far node -> repeater: "carry me"
    PKT_RELAY_UP    = 10,  // repeater -> controller: wraps a far node's HELLO / PAIR_OK / DATA
    PKT_RELAY_DOWN  = 11,  // controller -> repeater: wraps PAIR / UNPAIR / IDENTIFY / DATA_ACK / RELAY_CFG for a far node
    PKT_DATA_ACK    = 12,  // controller -> node (end-to-end: "reading seq N arrived")
    PKT_RELAY_PAIR  = 13,  // repeater -> far node: PAIR that also carries the controller's MAC
    PKT_NODE_INFO   = 14,  // node -> controller, every 60 s: relay on/off, children, parent
    PKT_RELAY_CFG   = 15,  // controller -> node: switch relaying on/off
};

enum SensorKind : uint8_t {
    SENSOR_NONE  = 0,
    SENSOR_SHT4X = 1,
    SENSOR_SHT3X = 2,
    SENSOR_AHT2X = 3,
    SENSOR_HTU21 = 4,
    SENSOR_SIM   = 9,   // bench: value typed on the serial port ("sim T RH")
};

struct __attribute__((packed)) PktHeader {
    uint8_t magic[2];
    uint8_t ver;
    uint8_t type;
};

struct __attribute__((packed)) PktHello {
    PktHeader h;
    uint8_t   paired;          // 1 = already paired (to ctrlMac)
    uint8_t   ctrlMac[6];
    char      fw[12];
};

struct __attribute__((packed)) PktAnnounce {
    PktHeader h;
    char      ctrlId[12];      // e.g. "FG1P_9E74"
    uint8_t   channel;
    uint8_t   knowsYou;        // 1 = you are on my paired list
};

struct __attribute__((packed)) PktPair {
    PktHeader h;
    char      ctrlId[12];
    uint8_t   channel;
};

struct __attribute__((packed)) PktData {
    PktHeader h;
    uint16_t  seq;
    int16_t   t_x100;          // deg C * 100
    uint16_t  rh_x100;         // %RH * 100
    uint8_t   sensorOk;
    uint8_t   sensorKind;      // SensorKind
    int8_t    rssiFromCtrl;    // RSSI of the last packet the node got from us (0 = unknown)
    uint32_t  uptimeSec;
    char      fw[12];
};

// ---- repeater version packets ----
struct __attribute__((packed)) PktRelayOffer {
    PktHeader h;
    char      ctrlId[12];
    int8_t    rssiToCtrl;      // repeater's signal from the controller (0 = unknown)
    uint8_t   children;        // far nodes it already carries
};

// Header + origin + RSSI, then the original packet (HELLO / PAIR_OK / DATA ...).
struct __attribute__((packed)) PktRelayUp {
    PktHeader h;
    uint8_t   origin[6];       // the far node
    int8_t    rssi;            // how strongly the repeater heard it
    uint8_t   innerLen;
    uint8_t   inner[48];       // only innerLen bytes are sent
};

struct __attribute__((packed)) PktRelayDown {
    PktHeader h;
    uint8_t   origin[6];       // the far node it is for
    uint8_t   innerLen;
    uint8_t   inner[40];       // only innerLen bytes are sent
};

struct __attribute__((packed)) PktDataAck {
    PktHeader h;
    uint8_t   origin[6];       // node the ack is for
    uint16_t  seq;
};

struct __attribute__((packed)) PktRelayPair {
    PktHeader h;
    char      ctrlId[12];
    uint8_t   channel;
    uint8_t   ctrlMac[6];      // the controller (the sender is the repeater)
};

struct __attribute__((packed)) PktNodeInfo {
    PktHeader h;
    uint8_t   relayOn;         // 1 = this node will carry far nodes
    uint8_t   children;        // far nodes it carries now
    uint8_t   parent[6];       // repeater it goes through (all 0 = direct)
    int8_t    rssiParent;      // signal from that repeater (0 = unknown)
};

struct __attribute__((packed)) PktRelayCfg {
    PktHeader h;
    uint8_t   origin[6];       // node it is for (used when sent through a repeater)
    uint8_t   relayOn;
};

inline size_t relayUpSize(uint8_t innerLen)   { return sizeof(PktRelayUp) - sizeof(((PktRelayUp*)0)->inner) + innerLen; }
inline size_t relayDownSize(uint8_t innerLen) { return sizeof(PktRelayDown) - sizeof(((PktRelayDown*)0)->inner) + innerLen; }

inline void protoFill(PktHeader& h, PktType t) {
    h.magic[0] = PROTO_MAGIC0; h.magic[1] = PROTO_MAGIC1; h.ver = PROTO_VER; h.type = t;
}
inline bool protoValid(const uint8_t* d, int len) {
    return len >= (int)sizeof(PktHeader) && d[0] == PROTO_MAGIC0 && d[1] == PROTO_MAGIC1 && d[2] == PROTO_VER;
}
