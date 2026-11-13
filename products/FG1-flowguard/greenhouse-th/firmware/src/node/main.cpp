// FG1 Greenhouse TH — Sensor Node (FG1 board, no relay).
// Reads T/RH and sends it to its paired controller over ESP-NOW every 10 s.
// Never joins WiFi; the controller is the only unit on the internet.
//
// LED (on-board, GPIO2):  slow blink 1 s = not paired (waiting to be added
//                         in the app) | fast blink = paired but controller
//                         not answering | double blink = paired, OK.
// Green LED (GPIO32):     short flash on every reading the controller ACKs.
// BOOT button held 5 s:   forget the controller (back to "not paired").
// Serial (115200):        "status", "forget", "send", "sim <T> <RH>" / "sim off" (fake
//                         reading for bench tests, RAM only) for bench testing.
// Repeater version (FG1_REPEATER, docs/repeater-plan.md): a node that can't
// reach the controller well goes through a nearer node, and a node that
// talks to the controller directly carries up to 4 far nodes. Extra serial
// commands: "relay on|off" (carry far nodes; saved), "nodirect on|off"
// (bench: pretend the controller is out of range; RAM only).
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "Config.h"
#include "EspNowProto.h"
#include "EspNowLink.h"
#include "THSensor.h"
#include "LEDManager.h"
#ifdef FG1_REPEATER
#include "RelayLogic.h"
#endif

enum NodeState { ST_UNPAIRED, ST_PAIRED, ST_LOST };
static const char* stateName[] = {"unpaired", "paired", "searching"};

static Preferences prefs;
static THSensor    sensor;

// Bench: "sim 33 40" makes the sensor report 33 C / 40 %RH; "sim off" returns to the real sensor.
static void simCommand(const String& c) {
    if (c == "sim off") { sensor.simOff(); sensor.read(); Serial.println("[SIM] off, real sensor"); return; }
    int sp = c.indexOf(' ', 4);
    if (sp < 0) { Serial.println("[SIM] usage: sim <T> <RH> | sim off"); return; }
    float t = c.substring(4, sp).toFloat(), rh = c.substring(sp + 1).toFloat();
    if (t < -20 || t > 80 || rh < 0 || rh > 100) { Serial.println("[SIM] out of range"); return; }
    sensor.simulate(t, rh); sensor.read();
    Serial.printf("[SIM] T=%.1f RH=%.1f (until 'sim off' or reboot)\n", t, rh);
}
static LEDManager  leds;

static NodeState state = ST_UNPAIRED;
static uint8_t   ctrlMac[6] = {0};
static char      ctrlId[12] = {0};
static uint8_t   ctrlCh = 1;
static String    nodeId;

static uint16_t  seq = 0;
static uint32_t  lastSendMs = 0;
static bool      awaitingAck = false;
static uint8_t   consecutiveFails = 0;
static int8_t    rssiFromCtrl = 0;
static uint32_t  identifyUntil = 0;

// Channel sweep (unpaired discovery / lost-controller search)
static bool      sweeping = false;
static uint8_t   sweepCh = 1;
static uint32_t  sweepDwellStart = 0;
static uint32_t  nextSweepAt = 0;
static const uint32_t SWEEP_DWELL_MS     = 220;
static const uint32_t UNPAIRED_PAUSE_MS  = 3000;
static const uint32_t LOST_PAUSE_MS      = 1000;
static const uint8_t  FAILS_BEFORE_LOST  = 3;
static const uint32_t RETRY_SEND_MS      = 2000;

static uint32_t  btnDownAt = 0;

#ifdef FG1_REPEATER
// ---- repeater version state ----
static uint8_t   myMac[6] = {0};
static uint8_t   parentMac[6] = {0};    // repeater we go through (all 0 = direct)
static int8_t    rssiParent = 0;
static bool      relayOn = true;        // carry far nodes (saved; app or "relay on|off")
static bool      ctrlAcks = false;      // controller sends DATA_ACK -> count delivery by it
static bool      waitingDataAck = false;
static uint16_t  ackWaitSeq = 0;
static uint32_t  ackWaitStart = 0;
static bool      noDirect = false;      // bench: ignore the controller's own packets
static relay::ChildTable children;
// what the current sweep heard
static bool      heardDirect = false, heardOffer = false;
static int8_t    directRssi = 0, offerBest = -128;
static uint8_t   offerMac[6] = {0};
static uint32_t  nextDirectCheck = 0;
static uint32_t  lastInfoMs = 0;
static const uint32_t DATA_ACK_TIMEOUT_MS = 1500;
static const uint32_t DIRECT_CHECK_MS     = 10UL * 60 * 1000;
static const uint32_t NODE_INFO_MS        = 60000;

static bool isZero(const uint8_t* m) { for (int i = 0; i < 6; i++) if (m[i]) return false; return true; }
static bool viaParent() { return !isZero(parentMac); }
// Carries far nodes only when paired directly to a controller that answers
// with DATA_ACK (a repeater-build controller; a star one would drop relayed packets).
static bool isRepeater() { return relayOn && state == ST_PAIRED && !viaParent() && ctrlAcks; }
static void sendNodeInfo();
#endif

static void saveCtrl() {
    prefs.putBool("paired", state != ST_UNPAIRED);
    prefs.putBytes("ctrl", ctrlMac, 6);
    prefs.putUChar("ch", ctrlCh);
    prefs.putString("cid", ctrlId);
#ifdef FG1_REPEATER
    prefs.putBytes("par", parentMac, 6);
    prefs.putBool("relay", relayOn);
#endif
}

static void forget(const char* why) {
    Serial.printf("[NODE] Forgetting controller (%s)\n", why);
    espnow.removePeer(ctrlMac);
    memset(ctrlMac, 0, 6);
    ctrlId[0] = 0;
    state = ST_UNPAIRED;
#ifdef FG1_REPEATER
    memset(parentMac, 0, 6);
    ctrlAcks = false;
    waitingDataAck = false;
    bool keepRelay = relayOn;
    prefs.clear();
    prefs.putBool("relay", keepRelay);
#else
    prefs.clear();
#endif
    sweeping = false;
    nextSweepAt = millis();
}

static void setState(NodeState s) {
    if (s == state) return;
    Serial.printf("[NODE] %s -> %s\n", stateName[state], stateName[s]);
    state = s;
}

static void sendHello() {
    PktHello p = {};
    protoFill(p.h, PKT_HELLO);
    p.paired = (state != ST_UNPAIRED);
    memcpy(p.ctrlMac, ctrlMac, 6);
    strlcpy(p.fw, FIRMWARE_VERSION, sizeof(p.fw));
    espnow.broadcast(&p, sizeof(p));
}

static void sendData() {
    sensor.read();
    PktData p = {};
    protoFill(p.h, PKT_DATA);
    p.seq        = ++seq;
    p.sensorOk   = sensor.ok();
    p.sensorKind = sensor.kind();
    p.t_x100     = (int16_t)lroundf(sensor.t() * 100);
    p.rh_x100    = (uint16_t)lroundf(sensor.rh() * 100);
    p.rssiFromCtrl = rssiFromCtrl;
    p.uptimeSec  = millis() / 1000;
    strlcpy(p.fw, FIRMWARE_VERSION, sizeof(p.fw));
#ifdef FG1_REPEATER
    lastSendMs = millis();
    if (noDirect && !viaParent()) {          // bench: out of range, nothing to send to
        consecutiveFails++;
        Serial.printf("[NODE] DATA #%u not sent (nodirect, no repeater)\n", p.seq);
        return;
    }
    bool sent = espnow.send(viaParent() ? parentMac : ctrlMac, &p, sizeof(p));
    if (viaParent() || ctrlAcks) {           // delivery = end-to-end DATA_ACK
        awaitingAck = false;
        waitingDataAck = sent;
        ackWaitSeq = p.seq;
        ackWaitStart = millis();
    } else {
        awaitingAck = sent;                  // delivery = radio ACK (star controller)
    }
    Serial.printf("[NODE] DATA #%u  T=%.2f RH=%.1f %s  ch%u%s\n", p.seq, sensor.t(), sensor.rh(),
                  sensor.ok() ? "" : "(sensor fault)", EspNowLink::channel(),
                  viaParent() ? (" via " + EspNowLink::macStr(parentMac)).c_str() : "");
    if (!sent) consecutiveFails++;
    return;
#endif
    awaitingAck = espnow.send(ctrlMac, &p, sizeof(p));
    lastSendMs = millis();
    Serial.printf("[NODE] DATA #%u  T=%.2f RH=%.1f %s  ch%u\n", p.seq, sensor.t(), sensor.rh(),
                  sensor.ok() ? "" : "(sensor fault)", EspNowLink::channel());
    if (!awaitingAck) consecutiveFails++;
}

#ifdef FG1_REPEATER
static void goDirect(const char* why) {
    if (viaParent()) Serial.printf("[NODE] Going direct to the controller (%s)\n", why);
    memset(parentMac, 0, 6);
    rssiParent = 0;
    saveCtrl();
}
static void resetHeard() { heardDirect = heardOffer = false; directRssi = 0; offerBest = -128; }

// End of a channel dwell while searching: take the best path heard on it.
static bool decideSweep() {
    if (state != ST_LOST || (!heardDirect && !heardOffer)) return false;
    relay::Path p = relay::choose(heardDirect, directRssi, heardOffer, offerBest);
    uint8_t ch = EspNowLink::channel();
    if (p == relay::Path::Direct) {
        goDirect("best path");
    } else {
        memcpy(parentMac, offerMac, 6);
        rssiParent = offerBest;
        PktHeader j; protoFill(j, PKT_RELAY_JOIN);
        espnow.send(parentMac, &j, sizeof(j));
        Serial.printf("[NODE] Going through repeater %s (path %d dBm)\n", EspNowLink::macStr(parentMac).c_str(), offerBest);
    }
    sweeping = false;
    if (ch != ctrlCh) ctrlCh = ch;
    saveCtrl();
    resetHeard();
    setState(ST_PAIRED);
    consecutiveFails = 0;
    sendData();
    sendNodeInfo();
    return true;
}
#endif

static void startSweep() {
#ifdef FG1_REPEATER
    resetHeard();
#endif
    sweeping = true;
    sweepCh = 1;
    EspNowLink::setChannel(sweepCh);
    sweepDwellStart = millis();
    sendHello();
}

static void stepSweep() {
    if (!sweeping) {
        if ((int32_t)(millis() - nextSweepAt) >= 0) startSweep();
        return;
    }
    if (millis() - sweepDwellStart < SWEEP_DWELL_MS) return;
#ifdef FG1_REPEATER
    if (decideSweep()) return;
#endif
    if (++sweepCh > 13) {
        sweeping = false;
        nextSweepAt = millis() + (state == ST_UNPAIRED ? UNPAIRED_PAUSE_MS : LOST_PAUSE_MS);
        // Park on the last known channel between sweeps, so a PAIR sent to
        // the old channel still has a chance.
        EspNowLink::setChannel(ctrlCh);
        return;
    }
    EspNowLink::setChannel(sweepCh);
    sweepDwellStart = millis();
    sendHello();
}

#ifdef FG1_REPEATER
static void relayUp(const EspNowRx& rx) {            // wrap a far node's packet for the controller
    PktRelayUp u = {};
    protoFill(u.h, PKT_RELAY_UP);
    memcpy(u.origin, rx.mac, 6);
    u.rssi = rx.rssi;
    u.innerLen = rx.len < sizeof(u.inner) ? rx.len : sizeof(u.inner);
    memcpy(u.inner, rx.data, u.innerLen);
    espnow.send(ctrlMac, &u, relayUpSize(u.innerLen));
}

// Repeater side. Returns true if the packet was for the repeater role.
static bool handleAsRepeater(const EspNowRx& rx, uint8_t type, bool fromCtrl) {
    if (!isRepeater()) return false;
    if (fromCtrl) {
        if (type != PKT_RELAY_DOWN || rx.len < relayDownSize(0)) return false;
        const PktRelayDown* d = (const PktRelayDown*)rx.data;
        if (d->innerLen < sizeof(PktHeader) || rx.len < relayDownSize(d->innerLen)) return true;
        uint8_t innerType = d->inner[3];
        if (innerType == PKT_PAIR && d->innerLen >= sizeof(PktPair)) {
            const PktPair* pp = (const PktPair*)d->inner;
            PktRelayPair rp = {};
            protoFill(rp.h, PKT_RELAY_PAIR);
            strlcpy(rp.ctrlId, pp->ctrlId, sizeof(rp.ctrlId));
            rp.channel = pp->channel;
            memcpy(rp.ctrlMac, ctrlMac, 6);
            children.touch(d->origin, millis());
            espnow.send(d->origin, &rp, sizeof(rp));
            Serial.printf("[RELAY] Pairing %s for the controller\n", EspNowLink::macStr(d->origin).c_str());
        } else {
            espnow.send(d->origin, d->inner, d->innerLen);
            if (innerType == PKT_UNPAIR) children.remove(d->origin);
        }
        return true;
    }
    // From another node.
    switch (type) {
    case PKT_HELLO: {
        if (rx.len < sizeof(PktHello)) return true;
        const PktHello* h = (const PktHello*)rx.data;
        if (!h->paired) { relayUp(rx); return true; }                // so the app lists it "via" us
        if (memcmp(h->ctrlMac, ctrlMac, 6) == 0 && children.canAccept(rx.mac)) {
            PktRelayOffer o = {};
            protoFill(o.h, PKT_RELAY_OFFER);
            strlcpy(o.ctrlId, ctrlId, sizeof(o.ctrlId));
            o.rssiToCtrl = rssiFromCtrl;
            o.children = children.count();
            espnow.send(rx.mac, &o, sizeof(o));
        }
        return true;
    }
    case PKT_RELAY_JOIN:
        if (children.touch(rx.mac, millis()))
            Serial.printf("[RELAY] Carrying %s (%u far node(s))\n", EspNowLink::macStr(rx.mac).c_str(), children.count());
        return true;
    case PKT_DATA:
    case PKT_PAIR_OK:
    case PKT_NODE_INFO:
        if (children.touch(rx.mac, millis())) relayUp(rx);           // it sent to us: it chose us
        return true;
    }
    return false;
}
#endif

static void handleRx(const EspNowRx& rx) {
    if (!protoValid(rx.data, rx.len)) return;
    uint8_t type = rx.data[3];
    bool fromCtrl = (state != ST_UNPAIRED) && memcmp(rx.mac, ctrlMac, 6) == 0;
#ifdef FG1_REPEATER
    if (noDirect && fromCtrl) return;                  // bench: controller "out of range"
    bool fromParent = viaParent() && memcmp(rx.mac, parentMac, 6) == 0;
    if (fromParent && rx.rssi) rssiParent = rx.rssi;
    if (handleAsRepeater(rx, type, fromCtrl)) return;
    bool fromPath = fromCtrl || fromParent;            // packets for us arrive on our path
#else
    bool fromPath = fromCtrl;
#endif
    if (fromCtrl && rx.rssi) rssiFromCtrl = rx.rssi;

    switch (type) {
    case PKT_ANNOUNCE: {
        if (rx.len < sizeof(PktAnnounce)) return;
        const PktAnnounce* a = (const PktAnnounce*)rx.data;
        if (state == ST_UNPAIRED) {
            Serial.printf("[NODE] Controller %.*s heard on ch%u (RSSI %d) — add this node in the app\n",
                          (int)sizeof(a->ctrlId), a->ctrlId, a->channel, rx.rssi);
        } else if (fromCtrl) {
#ifdef FG1_REPEATER
            if (state == ST_PAIRED && viaParent()) {           // reply to the periodic direct check
                if (relay::switchBackToDirect(rx.rssi)) { goDirect("direct signal good again"); sendNodeInfo(); }
                break;
            }
            if (sweeping && !relay::directGoodEnough(rx.rssi)) {   // weak: wait for repeater offers
                heardDirect = true;
                directRssi = rx.rssi;
                break;
            }
            goDirect("controller heard");
#endif
            uint8_t ch = EspNowLink::channel();
            sweeping = false;
            if (ch != ctrlCh) { ctrlCh = ch; saveCtrl(); }
            Serial.printf("[NODE] Found my controller on ch%u\n", ch);
            setState(ST_PAIRED);
            consecutiveFails = 0;
            sendData();
        }
        break;
    }
    case PKT_PAIR: {
        if (rx.len < sizeof(PktPair)) return;
        const PktPair* p = (const PktPair*)rx.data;
        if (state != ST_UNPAIRED && !fromCtrl) {
            Serial.println("[NODE] PAIR from another controller ignored (already paired)");
            return;
        }
        memcpy(ctrlMac, rx.mac, 6);
        strlcpy(ctrlId, p->ctrlId, sizeof(ctrlId));
        ctrlCh = EspNowLink::channel();
        sweeping = false;
#ifdef FG1_REPEATER
        memset(parentMac, 0, 6);
#endif
        setState(ST_PAIRED);
        saveCtrl();
        PktHeader ok; protoFill(ok, PKT_PAIR_OK);
        espnow.send(ctrlMac, &ok, sizeof(ok));
        Serial.printf("[NODE] Paired with %s on ch%u\n", ctrlId, ctrlCh);
        consecutiveFails = 0;
        lastSendMs = millis() - NODE_SEND_MS + 500;   // first reading right away
        break;
    }
    case PKT_UNPAIR:
        if (fromPath) forget("removed in app");
        break;
    case PKT_IDENTIFY:
        if (fromPath) { identifyUntil = millis() + 15000; Serial.println("[NODE] Identify"); }
        break;
#ifdef FG1_REPEATER
    case PKT_RELAY_OFFER: {
        if (rx.len < sizeof(PktRelayOffer) || state != ST_LOST || !sweeping) break;
        const PktRelayOffer* o = (const PktRelayOffer*)rx.data;
        int8_t score = relay::offerScore(rx.rssi, o->rssiToCtrl);
        if (!heardOffer || score > offerBest) {
            heardOffer = true;
            offerBest = score;
            memcpy(offerMac, rx.mac, 6);
        }
        break;
    }
    case PKT_RELAY_PAIR: {                             // added in the app, through a repeater
        if (rx.len < sizeof(PktRelayPair) || state != ST_UNPAIRED) break;
        const PktRelayPair* p = (const PktRelayPair*)rx.data;
        memcpy(ctrlMac, p->ctrlMac, 6);
        strlcpy(ctrlId, p->ctrlId, sizeof(ctrlId));
        memcpy(parentMac, rx.mac, 6);
        rssiParent = rx.rssi;
        ctrlCh = EspNowLink::channel();
        sweeping = false;
        setState(ST_PAIRED);
        saveCtrl();
        PktHeader ok; protoFill(ok, PKT_PAIR_OK);
        espnow.send(parentMac, &ok, sizeof(ok));
        Serial.printf("[NODE] Paired with %s through repeater %s on ch%u\n", ctrlId,
                      EspNowLink::macStr(parentMac).c_str(), ctrlCh);
        consecutiveFails = 0;
        lastSendMs = millis() - NODE_SEND_MS + 500;
        break;
    }
    case PKT_DATA_ACK: {
        if (rx.len < sizeof(PktDataAck) || !fromPath) break;
        const PktDataAck* a = (const PktDataAck*)rx.data;
        if (memcmp(a->origin, myMac, 6) != 0) break;
        if (fromCtrl && !ctrlAcks) {
            ctrlAcks = true;
            Serial.println("[NODE] Controller confirms readings (repeater version)");
            lastInfoMs = millis() - NODE_INFO_MS + 300;       // tell it soon that we can relay
        }
        if (waitingDataAck && a->seq == ackWaitSeq) {
            waitingDataAck = false;
            consecutiveFails = 0;
            leds.flowPulse();
        }
        break;
    }
    case PKT_RELAY_CFG: {
        if (rx.len < sizeof(PktRelayCfg) || !fromPath) break;
        relayOn = ((const PktRelayCfg*)rx.data)->relayOn;
        saveCtrl();
        Serial.printf("[NODE] Relaying for far nodes %s (from app)\n", relayOn ? "ON" : "OFF");
        sendNodeInfo();
        break;
    }
#endif
    }
}

static void updateLeds() {
    if ((int32_t)(identifyUntil - millis()) > 0) {
        bool on = (millis() / 100) % 2;
        digitalWrite(WIFI_LED_PIN, on);
        digitalWrite(FLOW_SENSOR_LED_PIN, !on);
        return;
    }
    switch (state) {
        case ST_UNPAIRED: leds.setWiFiState(WIFI_LED_AP_IDLE);   break;
        case ST_LOST:     leds.setWiFiState(WIFI_LED_SEARCHING); break;
        case ST_PAIRED:   leds.setWiFiState(WIFI_LED_FULL_OK);   break;
    }
    leds.loop();
}

#ifdef FG1_REPEATER
static void sendNodeInfo() {
    if (state != ST_PAIRED) return;
    PktNodeInfo i = {};
    protoFill(i.h, PKT_NODE_INFO);
    i.relayOn = relayOn;
    i.children = children.count();
    memcpy(i.parent, parentMac, 6);
    i.rssiParent = rssiParent;
    if (noDirect && !viaParent()) return;
    espnow.send(viaParent() ? parentMac : ctrlMac, &i, sizeof(i));
    lastInfoMs = millis();
}
#endif

static void printStatus() {
    Serial.printf("[NODE] id=%s fw=%s state=%s ctrl=%s (%s) ch=%u sensor=%s ok=%d T=%.2f RH=%.1f "
                  "acked=%lu failed=%lu rssiFromCtrl=%d\n",
                  nodeId.c_str(), FIRMWARE_VERSION, stateName[state], ctrlId,
                  EspNowLink::macStr(ctrlMac).c_str(), EspNowLink::channel(), sensor.name(),
                  sensor.ok(), sensor.t(), sensor.rh(),
                  (unsigned long)espnow.sentOk(), (unsigned long)espnow.sentFail(), rssiFromCtrl);
#ifdef FG1_REPEATER
    Serial.printf("[NODE] path=%s%s rssiParent=%d relay=%s carrying=%u ctrlAcks=%d nodirect=%d\n",
                  viaParent() ? "via " : "direct", viaParent() ? EspNowLink::macStr(parentMac).c_str() : "",
                  rssiParent, relayOn ? "on" : "off", children.count(), ctrlAcks, noDirect);
#endif
}


// Keep the radio TX power capped (RADIO_TX_POWER) across WiFi mode changes.
static void keepTxPower() {
#ifdef RADIO_TX_POWER
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    if (WiFi.getMode() != WIFI_OFF && WiFi.getTxPower() != RADIO_TX_POWER) {
        WiFi.setTxPower(RADIO_TX_POWER);
        Serial.printf("[WiFi] TX power capped at %.1f dBm\n", RADIO_TX_POWER / 4.0);
    }
#endif
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);                 // in case a relay is fitted
#ifdef PUMP_OUT_PIN
    pinMode(PUMP_OUT_PIN, OUTPUT);
    digitalWrite(PUMP_OUT_PIN, LOW);           // unused small-pump output stays OFF
#endif
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    leds.begin();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char id[16];
    snprintf(id, sizeof(id), "%s_%02X%02X", NODE_PREFIX, mac[4], mac[5]);
    nodeId = id;
#ifdef FG1_REPEATER
    memcpy(myMac, mac, 6);
    Serial.printf("\n[BOOT] FG1 Greenhouse TH Sensor Node %s  fw %s (repeater version)\n", nodeId.c_str(), FIRMWARE_VERSION);
#else
    Serial.printf("\n[BOOT] FG1 Greenhouse TH Sensor Node %s  fw %s\n", nodeId.c_str(), FIRMWARE_VERSION);
#endif

    Wire.begin(I2C_SDA, I2C_SCL);
    sensor.begin();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    espnow.begin();

    prefs.begin("fg1n", false);
#ifdef FG1_REPEATER
    relayOn = prefs.getBool("relay", true);
#endif
    if (prefs.getBool("paired", false) && prefs.getBytes("ctrl", ctrlMac, 6) == 6) {
        ctrlCh = prefs.getUChar("ch", 1);
        prefs.getString("cid", ctrlId, sizeof(ctrlId));
#ifdef FG1_REPEATER
        if (prefs.getBytesLength("par") == 6) prefs.getBytes("par", parentMac, 6);
        if (viaParent()) Serial.printf("[NODE] Through repeater %s\n", EspNowLink::macStr(parentMac).c_str());
#endif
        state = ST_PAIRED;
        EspNowLink::setChannel(ctrlCh);
        Serial.printf("[NODE] Paired with %s (%s), ch%u\n", ctrlId, EspNowLink::macStr(ctrlMac).c_str(), ctrlCh);
        lastSendMs = millis() - NODE_SEND_MS + 1000;
    } else {
        Serial.println("[NODE] Not paired — open the ASAC TH app > Sensors > Add");
        nextSweepAt = millis();
    }
}

void loop() {
    keepTxPower();
    EspNowRx rx;
    while (espnow.poll(rx)) handleRx(rx);

    if (state == ST_PAIRED) {
#ifdef FG1_REPEATER
        if (waitingDataAck && millis() - ackWaitStart >= DATA_ACK_TIMEOUT_MS) {
            waitingDataAck = false;
            consecutiveFails++;
            Serial.printf("[NODE] No DATA_ACK (%u in a row)\n", consecutiveFails);
        }
        children.expire(millis());
        if (millis() - lastInfoMs >= NODE_INFO_MS) sendNodeInfo();
        // A node on a repeater checks now and then whether the controller is reachable directly.
        if (viaParent() && !noDirect && (int32_t)(millis() - nextDirectCheck) >= 0) {
            nextDirectCheck = millis() + DIRECT_CHECK_MS;
            sendHello();
        }
#endif
        if (awaitingAck && espnow.sendDone()) {
            awaitingAck = false;
            if (espnow.lastSendOk()) {
                consecutiveFails = 0;
                leds.flowPulse();
            } else {
                consecutiveFails++;
                Serial.printf("[NODE] No ACK (%u in a row)\n", consecutiveFails);
            }
        }
        if (consecutiveFails >= FAILS_BEFORE_LOST) {
            setState(ST_LOST);
            consecutiveFails = 0;
#ifdef FG1_REPEATER
            // Relearn whether the controller confirms readings: it may have been
            // reflashed with the star firmware (then the radio ACK is all we get).
            ctrlAcks = false;
            waitingDataAck = false;
#endif
            nextSweepAt = millis();
        } else {
            uint32_t every = consecutiveFails ? RETRY_SEND_MS : NODE_SEND_MS;
#ifdef FG1_REPEATER
            if (waitingDataAck) every = 0xFFFFFFFF;
#endif
            if (!awaitingAck && millis() - lastSendMs >= every) sendData();
        }
    } else {
        stepSweep();
        // Keep the sensor warm so the first DATA after pairing is fresh.
        static uint32_t lastRead = 0;
        if (!sweeping && millis() - lastRead >= NODE_SEND_MS) { lastRead = millis(); sensor.read(); }
    }

    // BOOT button: hold 5 s to forget the controller.
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (!btnDownAt) btnDownAt = millis();
        else if (millis() - btnDownAt >= 5000) { forget("button"); btnDownAt = 0; while (digitalRead(BOOT_BUTTON_PIN) == LOW) delay(10); }
    } else btnDownAt = 0;

    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "status") printStatus();
        else if (cmd == "forget") forget("serial");
        else if (cmd == "send" && state != ST_UNPAIRED) sendData();
        else if (cmd.startsWith("sim")) { simCommand(cmd); if (state != ST_UNPAIRED) sendData(); }
#ifdef FG1_REPEATER
        else if (cmd == "relay on" || cmd == "relay off") {
            relayOn = cmd == "relay on"; saveCtrl(); sendNodeInfo();
            Serial.printf("[NODE] Relaying for far nodes %s\n", relayOn ? "ON" : "OFF");
        }
        else if (cmd == "nodirect on" || cmd == "nodirect off") {
            noDirect = cmd == "nodirect on";
            Serial.printf("[NODE] nodirect %s (bench: controller %s)\n", noDirect ? "ON" : "OFF",
                          noDirect ? "treated as out of range" : "reachable again");
            if (noDirect && !viaParent() && state == ST_PAIRED) { setState(ST_LOST); nextSweepAt = millis(); }
        }
#endif
    }

    static uint32_t lastStatus = 0;
    if (millis() - lastStatus >= 60000) { lastStatus = millis(); printStatus(); }

    updateLeds();
    delay(5);
}
