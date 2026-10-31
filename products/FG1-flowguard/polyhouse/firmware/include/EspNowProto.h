#pragma once
// ESP-NOW packets between an FG1 Polyhouse controller and its sensor nodes.
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
};

enum SensorKind : uint8_t {
    SENSOR_NONE  = 0,
    SENSOR_SHT4X = 1,
    SENSOR_SHT3X = 2,
    SENSOR_AHT2X = 3,
    SENSOR_HTU21 = 4,
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

inline void protoFill(PktHeader& h, PktType t) {
    h.magic[0] = PROTO_MAGIC0; h.magic[1] = PROTO_MAGIC1; h.ver = PROTO_VER; h.type = t;
}
inline bool protoValid(const uint8_t* d, int len) {
    return len >= (int)sizeof(PktHeader) && d[0] == PROTO_MAGIC0 && d[1] == PROTO_MAGIC1 && d[2] == PROTO_VER;
}
