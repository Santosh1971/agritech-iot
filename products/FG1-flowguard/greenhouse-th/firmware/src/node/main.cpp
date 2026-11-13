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
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_mac.h>
#include "Config.h"
#include "EspNowProto.h"
#include "EspNowLink.h"
#include "THSensor.h"
#include "LEDManager.h"

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

static void saveCtrl() {
    prefs.putBool("paired", state != ST_UNPAIRED);
    prefs.putBytes("ctrl", ctrlMac, 6);
    prefs.putUChar("ch", ctrlCh);
    prefs.putString("cid", ctrlId);
}

static void forget(const char* why) {
    Serial.printf("[NODE] Forgetting controller (%s)\n", why);
    espnow.removePeer(ctrlMac);
    memset(ctrlMac, 0, 6);
    ctrlId[0] = 0;
    state = ST_UNPAIRED;
    prefs.clear();
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
    awaitingAck = espnow.send(ctrlMac, &p, sizeof(p));
    lastSendMs = millis();
    Serial.printf("[NODE] DATA #%u  T=%.2f RH=%.1f %s  ch%u\n", p.seq, sensor.t(), sensor.rh(),
                  sensor.ok() ? "" : "(sensor fault)", EspNowLink::channel());
    if (!awaitingAck) consecutiveFails++;
}

static void startSweep() {
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

static void handleRx(const EspNowRx& rx) {
    if (!protoValid(rx.data, rx.len)) return;
    uint8_t type = rx.data[3];
    bool fromCtrl = (state != ST_UNPAIRED) && memcmp(rx.mac, ctrlMac, 6) == 0;
    if (fromCtrl && rx.rssi) rssiFromCtrl = rx.rssi;

    switch (type) {
    case PKT_ANNOUNCE: {
        if (rx.len < sizeof(PktAnnounce)) return;
        const PktAnnounce* a = (const PktAnnounce*)rx.data;
        if (state == ST_UNPAIRED) {
            Serial.printf("[NODE] Controller %.*s heard on ch%u (RSSI %d) — add this node in the app\n",
                          (int)sizeof(a->ctrlId), a->ctrlId, a->channel, rx.rssi);
        } else if (fromCtrl) {
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
        if (fromCtrl) forget("removed in app");
        break;
    case PKT_IDENTIFY:
        if (fromCtrl) { identifyUntil = millis() + 15000; Serial.println("[NODE] Identify"); }
        break;
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

static void printStatus() {
    Serial.printf("[NODE] id=%s fw=%s state=%s ctrl=%s (%s) ch=%u sensor=%s ok=%d T=%.2f RH=%.1f "
                  "acked=%lu failed=%lu rssiFromCtrl=%d\n",
                  nodeId.c_str(), FIRMWARE_VERSION, stateName[state], ctrlId,
                  EspNowLink::macStr(ctrlMac).c_str(), EspNowLink::channel(), sensor.name(),
                  sensor.ok(), sensor.t(), sensor.rh(),
                  (unsigned long)espnow.sentOk(), (unsigned long)espnow.sentFail(), rssiFromCtrl);
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
    Serial.printf("\n[BOOT] FG1 Greenhouse TH Sensor Node %s  fw %s\n", nodeId.c_str(), FIRMWARE_VERSION);

    Wire.begin(I2C_SDA, I2C_SCL);
    sensor.begin();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);
    espnow.begin();

    prefs.begin("fg1n", false);
    if (prefs.getBool("paired", false) && prefs.getBytes("ctrl", ctrlMac, 6) == 6) {
        ctrlCh = prefs.getUChar("ch", 1);
        prefs.getString("cid", ctrlId, sizeof(ctrlId));
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
            nextSweepAt = millis();
        } else {
            uint32_t every = consecutiveFails ? RETRY_SEND_MS : NODE_SEND_MS;
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
    }

    static uint32_t lastStatus = 0;
    if (millis() - lastStatus >= 60000) { lastStatus = millis(); printStatus(); }

    updateLeds();
    delay(5);
}
