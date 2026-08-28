#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Forward declarations -- several early helper functions (delayWithLeds,
// listenForJoin, etc.) call these before their real definitions appear
// later in the file. Plain .cpp, no Arduino auto-prototyping, so without
// these the compiler rejects the call. Same fix for the recurring
// ordering issue we've hit several times on this project.
void updateInputs();
void applyLevelLogic();

// ---------------------------------------------------------------------
// WPC Master Node
// Reads 3 water-level float switches + 1 "No Power" input, dynamically
// accepts Pump Node JOIN_REQUESTs and assigns each a compact wire-slot,
// then runs a round-robin poll cycle sending LEVEL_CMD to known slots.
//
// PROTOCOL (see docs/WPC_LoRa_Protocol_v0.1.md):
//   [version:1][msgType:1][masterId:4][pumpSlot:1][seq:1][payload...][crc16:2]
// ---------------------------------------------------------------------

#define LORA_FREQ_MHZ 866.0
#define LORA_BW_KHZ   125.0
#define LORA_SF       9
#define LORA_CR       7
#define LORA_TXPOWER  14
#define LORA_SYNCWORD 0x12

#define PIN_NSS    5
#define PIN_SCK    18
#define PIN_MOSI   23
#define PIN_MISO   19
#define PIN_RESET  25
#define PIN_BUSY   27
#define PIN_DIO1   26

#define PIN_IN1        36
#define PIN_IN2        39
#define PIN_IN3        34
#define PIN_IN4        35
#define PIN_IN1_LED    32
#define PIN_IN2_LED    33
#define PIN_IN3_LED    14
#define PIN_IN4_LED    13
#define PIN_LORA_LED   4    // dedicated LoRa activity LED (from schematic)
#define PIN_WIFI_LED   2    // dev kit's own "WiFi" onboard LED (separate from the hardwired-always-on Power LED)

#define LEVEL_ACTIVE_STATE LOW

#define DEBOUNCE_MS_DEFAULT   10000UL   // 10s -- programmable range is 10s-5min
#define DEBOUNCE_MS_MIN        10000UL
#define DEBOUNCE_MS_MAX       300000UL
uint32_t levelDebounceMs = DEBOUNCE_MS_DEFAULT;
#define POLL_TIMEOUT_MS     500
#define POLL_RETRIES        2
#define STAGGER_MS          5000
#define JOIN_WINDOW_MS       2000   // widened -- was missing joins too often against the Pump's 3s retry cadence

enum MsgType : uint8_t {
  MSG_JOIN_REQUEST = 0x01,
  MSG_JOIN_ACCEPT  = 0x02,
  MSG_LEVEL_CMD    = 0x10,
  MSG_CMD_ACK      = 0x11,
};

#define PROTO_VERSION 1
#define MAX_PUMPS 20

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);
Preferences prefs;
uint16_t storedPumpIds[MAX_PUMPS];
WebServer server(80);
bool wifiApOk = false;   // set from WiFi.softAP()'s own return value

// Blocking blink -- used for infrequent, short-lived LoRa TX/RX indicators.
// Fine here since it's only ever called right after a TX/RX event already
// confirmed complete, not inside a timing-critical wait window.
void blinkLed(uint8_t pin, int times, int onMs = 60, int offMs = 60) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(onMs);
    digitalWrite(pin, LOW);
    if (i < times - 1) delay(offMs);
  }
}

// Non-blocking repeating pattern for the WiFi LED -- double-blink heartbeat
// when the SoftAP started OK, fast single-blink if it failed. Must be
// called frequently (main loop AND inside any blocking wait loops) since
// it only advances based on millis(), never actually delays.
bool loraLinkError = false;   // true when the most recently completed poll cycle got zero ACKs from any known pump

// Fast continuous blink to flag "nothing is responding" -- only takes
// over the LoRa LED when loraLinkError is true; otherwise leaves the LED
// alone so the momentary 1x-send/2x-receive blinkLed() calls keep working
// exactly as before. The two never really compete: this pattern only
// shows during stretches with zero ACKs, and the discrete double-blink
// only shows right when an ACK succeeds -- by definition not overlapping.
void updateLoraLed() {
  if (!loraLinkError) return;
  static uint32_t phaseStart = 0;
  static uint8_t phaseIdx = 0;
  static const bool errPattern[] = {true, false};
  static const uint16_t errDur[] = {80, 80};

  uint32_t now = millis();
  if (now - phaseStart >= errDur[phaseIdx]) {
    phaseIdx = (phaseIdx + 1) % 2;
    phaseStart = now;
  }
  digitalWrite(PIN_LORA_LED, errPattern[phaseIdx] ? HIGH : LOW);
}

int wifiStationCount = 0;

// Three distinct patterns: slow double-blink-then-pause (idle, AP up, no
// phone connected), continuous fast blink with no pause (a phone IS
// connected right now -- this is what Avinash asked for, to see at a
// glance which device his phone is actually talking to), or a slower
// continuous blink (SoftAP itself failed to start). Speeds are kept
// clearly different (50ms vs 150ms) so "connected" and "error" don't
// look the same at a glance.
void updateWifiLed() {
  static uint32_t lastStationCheck = 0;
  static uint32_t phaseStart = 0;
  static uint8_t phaseIdx = 0;

  uint32_t now = millis();
  if (now - lastStationCheck >= 500) {
    wifiStationCount = WiFi.softAPgetStationNum();
    lastStationCheck = now;
  }

  static const bool idlePattern[]      = {true, false, true, false};
  static const uint16_t idleDur[]      = {80, 80, 80, 800};
  static const bool connectedPattern[] = {true, false};
  static const uint16_t connectedDur[] = {50, 50};
  static const bool errPattern[]       = {true, false};
  static const uint16_t errDur[]       = {150, 150};

  const bool* pattern;
  const uint16_t* durations;
  uint8_t patternLen;

  if (!wifiApOk) {
    pattern = errPattern; durations = errDur; patternLen = 2;
  } else if (wifiStationCount > 0) {
    pattern = connectedPattern; durations = connectedDur; patternLen = 2;
  } else {
    pattern = idlePattern; durations = idleDur; patternLen = 4;
  }

  if (now - phaseStart >= durations[phaseIdx]) {
    phaseIdx = (phaseIdx + 1) % patternLen;
    phaseStart = now;
  }
  digitalWrite(PIN_WIFI_LED, pattern[phaseIdx] ? HIGH : LOW);
}

// Non-blocking replacement for plain delay() -- keeps the WiFi and LoRa
// LED patterns (and the web server) alive during long waits, instead of
// freezing them for the whole duration like a bare delay() does.
void delayWithLeds(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    server.handleClient();
    updateWifiLed();
    updateLoraLed();
    updateInputs();
    applyLevelLogic();
    delay(5);
  }
}   // NVS-backed slot->pumpId mapping, 0 = empty

volatile bool operationDone = false;

void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
}

uint32_t masterId32 = 0;
uint8_t  txSeq = 0;

uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

struct PumpEntry {
  uint8_t  slot;
  bool     known;
  uint16_t pumpId;
  uint8_t  assignedLevels;  // bitmask -- bit (L-1) set means this pump is assigned to level L. 0 = unassigned (stays OFF)
  char     name[16];        // installer-friendly label, empty = show pumpId instead
  bool     lastRelayState;
  bool     online;
  bool     everAttempted;   // false right after boot/restore -- online defaults false too but that
                             // does NOT mean "confirmed offline", just "not yet checked this session"
};
PumpEntry pumps[MAX_PUMPS];

uint8_t numLevels = 3;   // configurable 1-3, persisted in NVS

struct StoredPump { uint16_t pumpId; uint8_t levelMask; char name[16]; };
StoredPump storedPumps[MAX_PUMPS];

void initPumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) {
    pumps[i].slot = (uint8_t)i;
    pumps[i].known = false;
    pumps[i].pumpId = 0;
    pumps[i].assignedLevels = 0;
    pumps[i].name[0] = '\0';
    pumps[i].lastRelayState = false;
    pumps[i].online = false;
    pumps[i].everAttempted = false;
  }
}

void savePumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) {
    storedPumps[i].pumpId = pumps[i].known ? pumps[i].pumpId : 0;
    storedPumps[i].levelMask = pumps[i].assignedLevels;
    strncpy(storedPumps[i].name, pumps[i].name, sizeof(storedPumps[i].name) - 1);
    storedPumps[i].name[sizeof(storedPumps[i].name) - 1] = '\0';
  }
  prefs.putBytes("pumpTable", storedPumps, sizeof(storedPumps));
}

void loadPumpTable() {
  size_t n = prefs.getBytes("pumpTable", storedPumps, sizeof(storedPumps));
  if (n != sizeof(storedPumps)) {
    memset(storedPumps, 0, sizeof(storedPumps));
    return;
  }
  for (int i = 0; i < MAX_PUMPS; i++) {
    if (storedPumps[i].pumpId != 0) {
      pumps[i].known = true;
      pumps[i].pumpId = storedPumps[i].pumpId;
      pumps[i].assignedLevels = storedPumps[i].levelMask;
      strncpy(pumps[i].name, storedPumps[i].name, sizeof(pumps[i].name) - 1);
      pumps[i].name[sizeof(pumps[i].name) - 1] = '\0';
      Serial.print(F("[NVS] restored slot "));
      Serial.print(i);
      Serial.print(F(" -> pumpId "));
      Serial.print(storedPumps[i].pumpId);
      Serial.print(F(" levelMask "));
      Serial.println(storedPumps[i].levelMask, BIN);
    }
  }
}

int findSlotByPumpId(uint16_t pumpId) {
  for (int i = 0; i < MAX_PUMPS; i++) if (pumps[i].known && pumps[i].pumpId == pumpId) return i;
  return -1;
}

int findFreeSlot() {
  for (int i = 0; i < MAX_PUMPS; i++) if (!pumps[i].known) return i;
  return -1;
}

struct LevelInput {
  uint8_t pin;
  uint8_t ledPin;
  bool    state;
  bool    rawLast;
  uint32_t lastChangeMs;
};

LevelInput inputs[4] = {
  {PIN_IN1, PIN_IN1_LED, false, false, 0},
  {PIN_IN2, PIN_IN2_LED, false, false, 0},
  {PIN_IN3, PIN_IN3_LED, false, false, 0},
  {PIN_IN4, PIN_IN4_LED, false, false, 0},
};

// IN4 (No Power) is an alert condition, not an informational level --
// its LED fast-blinks while active rather than staying steady on, so it
// reads visually differently from IN1-IN3.
bool fastBlinkOn(uint32_t intervalMs) {
  return (millis() / intervalMs) % 2 == 0;
}

// Asymmetric "minimum dwell" logic, not a symmetric debounce: a genuine
// level change is accepted INSTANTLY as soon as it's eligible (enough
// time has passed since the last accepted change), but any reading that
// disagrees with the currently-accepted state is otherwise ignored
// entirely until that time has elapsed -- so a real crossing reacts
// immediately, while a bounce/wobble right at the threshold (waves,
// pump vibration) can't flip it back and forth. lastChangeMs here means
// "when we last ACCEPTED a change", not "when the raw signal last moved".
void updateInputs() {
  uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    LevelInput& in = inputs[i];
    bool raw = (digitalRead(in.pin) == LEVEL_ACTIVE_STATE);
    if (raw != in.state && (now - in.lastChangeMs) >= levelDebounceMs) {
      in.state = raw;
      in.lastChangeMs = now;
      Serial.print(F("[LEVEL] pin "));
      Serial.print(in.pin);
      Serial.print(F(" -> "));
      Serial.println(in.state ? F("ACTIVE") : F("CLEAR"));
    }
    bool isNoPower = (i == 3);
    if (isNoPower && in.state) {
      digitalWrite(in.ledPin, fastBlinkOn(150) ? HIGH : LOW);
    } else {
      digitalWrite(in.ledPin, in.state ? HIGH : LOW);
    }
  }
}

uint8_t txPacket[32];

size_t buildPacket(uint8_t msgType, uint8_t pumpSlot, const uint8_t* payload, size_t payloadLen) {
  size_t i = 0;
  txPacket[i++] = PROTO_VERSION;
  txPacket[i++] = msgType;
  txPacket[i++] = (masterId32 >> 24) & 0xFF;
  txPacket[i++] = (masterId32 >> 16) & 0xFF;
  txPacket[i++] = (masterId32 >> 8) & 0xFF;
  txPacket[i++] = masterId32 & 0xFF;
  txPacket[i++] = pumpSlot;
  txPacket[i++] = txSeq++;
  for (size_t p = 0; p < payloadLen; p++) txPacket[i++] = payload[p];
  uint16_t crc = crc16(txPacket, i);
  txPacket[i++] = (crc >> 8) & 0xFF;
  txPacket[i++] = crc & 0xFF;
  return i;
}

// Listens briefly for JOIN_REQUEST from not-yet-known pumps and
// dynamically assigns the next free slot -- this is the "default PN
// registration" mechanism: no app needed, no pre-guessed ID list.
void listenForJoin(uint32_t windowMs) {
  operationDone = false;
  radio.startReceive();
  uint32_t start = millis();
  while (millis() - start < windowMs) {
    server.handleClient();
    updateWifiLed();
    updateLoraLed();
    updateInputs();
    applyLevelLogic();
    if (operationDone) {
      operationDone = false;
      uint8_t buf[32];
      int len = radio.getPacketLength();
      int state = radio.readData(buf, len);
      if (state == RADIOLIB_ERR_NONE && len >= 10) {
        uint16_t rxCrc = (buf[len - 2] << 8) | buf[len - 1];
        uint32_t reqMasterId = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) |
                                ((uint32_t)buf[4] << 8) | buf[5];
        // Must actually be addressed to THIS Master -- without this check,
        // any Master within range would accept a join meant for a
        // different one, defeating the point of targetMasterId entirely.
        if (crc16(buf, len - 2) == rxCrc && buf[1] == MSG_JOIN_REQUEST &&
            reqMasterId == masterId32) {
          uint16_t pumpId = ((uint16_t)buf[8] << 8) | buf[9];
          int slot = findSlotByPumpId(pumpId);
          if (slot < 0) slot = findFreeSlot();
          if (slot >= 0) {
            pumps[slot].known = true;
            pumps[slot].pumpId = pumpId;
            // A fresh join is a genuinely new chance to reconnect --
            // reset everAttempted so it gets the fair, generous first
            // poll (full retries/timeout), not the reduced budget left
            // over from whatever happened to this slot before.
            pumps[slot].everAttempted = false;
            savePumpTable();
            Serial.print(F("[JOIN] pumpId "));
            Serial.print(pumpId);
            Serial.print(F(" -> slot "));
            Serial.println(slot);
            blinkLed(PIN_LORA_LED, 2);   // 2 blinks = we received the JOIN_REQUEST

            // Echo the accepted pumpId back in the payload -- JOIN_ACCEPT
            // is broadcast over LoRa, so any other unjoined pump hearing
            // it must be able to tell it wasn't addressed to them.
            uint8_t payload[3] = { (uint8_t)(pumpId >> 8), (uint8_t)(pumpId & 0xFF), (uint8_t)slot };
            size_t alen = buildPacket(MSG_JOIN_ACCEPT, (uint8_t)slot, payload, 3);
            operationDone = false;
            radio.startTransmit(txPacket, alen);
            uint32_t t0 = millis();
            while (!operationDone && millis() - t0 < 1000) { updateWifiLed(); updateLoraLed(); }
            operationDone = false;
            blinkLed(PIN_LORA_LED, 1);   // 1 blink = we sent the JOIN_ACCEPT
          } else {
            Serial.println(F("[JOIN] no free slot"));
          }
        }
      }
      radio.startReceive();
    }
  }
}

bool pollPump(uint8_t slot, bool desired, uint32_t txTimeoutMs, uint32_t rxTimeoutMs) {
  uint8_t payload[1] = { (uint8_t)(desired ? 1 : 0) };
  size_t len = buildPacket(MSG_LEVEL_CMD, slot, payload, 1);

  operationDone = false;
  int state = radio.startTransmit(txPacket, len);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] startTransmit failed, code "));
    Serial.println(state);
    return false;
  }

  uint32_t t0 = millis();
  while (!operationDone) {
    server.handleClient();
    updateWifiLed();
    updateLoraLed();
    if (millis() - t0 > txTimeoutMs) {
      Serial.println(F("[LoRa] TX timeout"));
      return false;
    }
  }
  operationDone = false;

  Serial.print(F("[POLL] slot "));
  Serial.print(slot);
  Serial.print(F(" LEVEL_CMD -> "));
  Serial.println(desired ? F("ON") : F("OFF"));
  blinkLed(PIN_LORA_LED, 1);   // 1 blink = we sent something

  radio.startReceive();
  uint32_t start = millis();
  while (millis() - start < rxTimeoutMs) {
    server.handleClient();
    updateWifiLed();
    updateLoraLed();
    if (operationDone) {
      operationDone = false;
      uint8_t buf[32];
      int rlen = radio.getPacketLength();
      int rstate = radio.readData(buf, rlen);
      if (rstate == RADIOLIB_ERR_NONE && rlen >= 10) {
        if (buf[1] == MSG_CMD_ACK && buf[6] == slot) {
          Serial.print(F("[POLL] CMD_ACK from slot "));
          Serial.println(slot);
          blinkLed(PIN_LORA_LED, 2);   // 2 blinks = we received something back
          return true;
        }
      }
      radio.startReceive();
    }
  }
  return false;
}

bool desiredPumpState[MAX_PUMPS] = { false };

void applyLevelLogic() {
  for (int slot = 0; slot < MAX_PUMPS; slot++) {
    if (!pumps[slot].known) { desiredPumpState[slot] = false; continue; }
    bool on = false;
    for (int lvl = 1; lvl <= numLevels; lvl++) {
      // Real float switches close (short) when water RISES and lifts them,
      // opposite of the bench DIP switches -- so "not yet reached" (switch
      // still open) is what should drive the pump ON, not the reverse. The
      // raw reading and the LEDs stay unflipped (LED ON still correctly
      // means "water physically present at this level") -- only this
      // control condition is inverted.
      if ((pumps[slot].assignedLevels & (1 << (lvl - 1))) && !inputs[lvl - 1].state) {
        on = true;
        break;
      }
    }
    desiredPumpState[slot] = on;
  }
}

void pollCycle() {
  // wasOn tracks each pump's last CONFIRMED (acked) relay state, not
  // "have we ever commanded it" -- that's what lets us correctly detect
  // a fresh OFF->ON transition every time, not just the first time ever.
  static bool wasOn[MAX_PUMPS] = { false };
  int freshOnCountThisPass = 0;
  bool anyKnownPump = false;
  bool anyAckedThisPass = false;

  for (int slot = 0; slot < MAX_PUMPS; slot++) {
    if (!pumps[slot].known) continue;
    anyKnownPump = true;

    bool desired = desiredPumpState[slot];
    bool turningOn = desired && !wasOn[slot];

    // stagger only between MULTIPLE pumps freshly turning ON in the
    // same pass -- the first one in a pass never waits
    if (turningOn && freshOnCountThisPass > 0) delayWithLeds(STAGGER_MS);
    if (turningOn) freshOnCountThisPass++;

    // A slot only gets the short, single-shot check after it has
    // genuinely been tried and failed at least once THIS session --
    // online defaults false on every boot (it's never persisted), so
    // without the everAttempted check every freshly-restored pump would
    // wrongly get the aggressive short timeout on its very first ever
    // poll, before it had any real chance to reconnect. Once confirmed
    // offline by a real generous attempt, reduce the budget so a
    // known-dead slot doesn't burn ~7.5s/cycle starving the 2s
    // join-listening window.
    bool giveFullBudget = pumps[slot].online || !pumps[slot].everAttempted;
    int maxAttempts = giveFullBudget ? (POLL_RETRIES + 1) : 1;
    uint32_t txTimeout = giveFullBudget ? 2000 : 500;
    uint32_t rxTimeout = giveFullBudget ? POLL_TIMEOUT_MS : 200;

    bool acked = false;
    for (int attempt = 0; attempt < maxAttempts && !acked; attempt++) {
      acked = pollPump(slot, desired, txTimeout, rxTimeout);
    }

    pumps[slot].everAttempted = true;
    pumps[slot].online = acked;
    if (acked) {
      pumps[slot].lastRelayState = desired;
      wasOn[slot] = desired;
      anyAckedThisPass = true;
    } else {
      Serial.print(F("[POLL] slot "));
      Serial.print(slot);
      Serial.println(F(" NOT ACKED -- marked offline"));
    }
  }

  loraLinkError = anyKnownPump && !anyAckedThisPass;
}

// GET /status -- JSON snapshot of sump levels + known pumps, for the
// app to poll while connected to this Master's SoftAP.
void handleStatus() {
  JsonDocument doc;
  char idbuf[12];
  snprintf(idbuf, sizeof(idbuf), "0x%08X", masterId32);
  doc["masterId"] = idbuf;
  doc["numLevels"] = numLevels;
  doc["debounceMs"] = levelDebounceMs;

  JsonArray levelsArr = doc["levels"].to<JsonArray>();
  for (int i = 0; i < numLevels; i++) levelsArr.add(inputs[i].state);
  doc["noPower"] = inputs[3].state;   // polarity TBD -- raw state, see docs

  JsonArray pumpsArr = doc["pumps"].to<JsonArray>();
  for (int i = 0; i < MAX_PUMPS; i++) {
    if (!pumps[i].known) continue;
    JsonObject p = pumpsArr.add<JsonObject>();
    p["slot"] = pumps[i].slot;
    p["pumpId"] = pumps[i].pumpId;
    p["online"] = pumps[i].online;
    p["relay"] = pumps[i].lastRelayState;
    p["desired"] = desiredPumpState[i];
    JsonArray lvls = p["assignedLevels"].to<JsonArray>();
    for (int lvl = 1; lvl <= 3; lvl++) {
      if (pumps[i].assignedLevels & (1 << (lvl - 1))) lvls.add(lvl);
    }
    p["name"] = pumps[i].name;
  }

  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

// POST /config  body: {"numLevels": 1-3}
void handleSetConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  if (doc["numLevels"].is<int>()) {
    int n = doc["numLevels"];
    if (n >= 1 && n <= 3) {
      numLevels = (uint8_t)n;
      prefs.putUChar("numLevels", numLevels);
    }
  }
  if (doc["debounceMs"].is<unsigned long>() || doc["debounceMs"].is<int>()) {
    unsigned long d = doc["debounceMs"];
    if (d >= DEBOUNCE_MS_MIN && d <= DEBOUNCE_MS_MAX) {
      levelDebounceMs = (uint32_t)d;
      prefs.putULong("debounceMs", levelDebounceMs);
    }
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /assign  body: {"slot": N, "level": 1-3, "assigned": true/false}
// Toggles ONE level's membership -- a pump can now have multiple levels set.
void handleAssign() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  int slot = doc["slot"] | -1;
  int level = doc["level"] | -1;
  bool assigned = doc["assigned"] | false;
  if (slot < 0 || slot >= MAX_PUMPS || !pumps[slot].known || level < 1 || level > 3) {
    server.send(400, "application/json", "{\"error\":\"invalid slot/level\"}");
    return;
  }
  uint8_t bit = 1 << (level - 1);
  if (assigned) pumps[slot].assignedLevels |= bit;
  else pumps[slot].assignedLevels &= ~bit;
  savePumpTable();
  Serial.print(F("[ASSIGN] slot "));
  Serial.print(slot);
  Serial.print(F(" level "));
  Serial.print(level);
  Serial.println(assigned ? F(" -> ON") : F(" -> OFF"));
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /name  body: {"slot": N, "name": "..."}
void handleSetName() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  int slot = doc["slot"] | -1;
  const char* name = doc["name"] | "";
  if (slot < 0 || slot >= MAX_PUMPS || !pumps[slot].known) {
    server.send(400, "application/json", "{\"error\":\"invalid slot\"}");
    return;
  }
  strncpy(pumps[slot].name, name, sizeof(pumps[slot].name) - 1);
  pumps[slot].name[sizeof(pumps[slot].name) - 1] = '\0';
  savePumpTable();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

// POST /forget  body: {"slot": N}
// Clears a slot entirely -- for removing a stale/orphaned pump entry
// (e.g. one that was reprovisioned to a different pumpId and will never
// come back under its old identity).
void handleForget() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= MAX_PUMPS || !pumps[slot].known) {
    server.send(400, "application/json", "{\"error\":\"invalid slot\"}");
    return;
  }
  pumps[slot].known = false;
  pumps[slot].pumpId = 0;
  pumps[slot].assignedLevels = 0;
  pumps[slot].name[0] = '\0';
  pumps[slot].lastRelayState = false;
  pumps[slot].online = false;
  savePumpTable();
  Serial.print(F("[FORGET] slot "));
  Serial.println(slot);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"ok\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  for (auto& in : inputs) {
    pinMode(in.pin, INPUT);
    pinMode(in.ledPin, OUTPUT);
    digitalWrite(in.ledPin, LOW);
  }

  // Self-test: all 6 LEDs (4 level LEDs incl. No-Power alert, LoRa
  // activity, and the dev kit's onboard WiFi LED) ON for 3s then OFF --
  // NOT the hardwired-always-on Power LED, which isn't GPIO controlled.
  pinMode(PIN_LORA_LED, OUTPUT);
  pinMode(PIN_WIFI_LED, OUTPUT);
  Serial.println(F("[SELFTEST] LEDs ON"));
  for (auto& in : inputs) digitalWrite(in.ledPin, HIGH);
  digitalWrite(PIN_LORA_LED, HIGH);
  digitalWrite(PIN_WIFI_LED, HIGH);
  delay(3000);
  for (auto& in : inputs) digitalWrite(in.ledPin, LOW);
  digitalWrite(PIN_LORA_LED, LOW);
  digitalWrite(PIN_WIFI_LED, LOW);
  Serial.println(F("[SELFTEST] LEDs OFF"));

  uint64_t mac = ESP.getEfuseMac();
  masterId32 = (uint32_t)(mac & 0xFFFFFFFF);
  Serial.print(F("[MASTER] ID: 0x"));
  Serial.println(masterId32, HEX);

  prefs.begin("wpc", false);
  numLevels = prefs.getUChar("numLevels", 3);
  levelDebounceMs = prefs.getULong("debounceMs", DEBOUNCE_MS_DEFAULT);
  // Seed each input's lastChangeMs so the very first real reading at
  // boot is treated as immediately eligible, not locked out for up to
  // a full debounce period right after power-on.
  for (auto& in : inputs) in.lastChangeMs = millis() - levelDebounceMs - 1;
  initPumpTable();
  loadPumpTable();

  loraSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                           LORA_SYNCWORD, LORA_TXPOWER, 8, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] radio.begin() failed, code "));
    Serial.println(state);
    while (true) delay(1000);
  }
  Serial.println(F("[LoRa] Radio initialized OK."));
  radio.setDio1Action(onRadioAction);

  // Open AP for now -- pairing/network security deferred, matching the
  // earlier project decision (see docs). App connects directly to this
  // SoftAP to reach the status endpoint.
  char apSuffix[9];
  snprintf(apSuffix, sizeof(apSuffix), "%08X", (unsigned int)masterId32);
  String apSsid = "WPC-Master-" + String(apSuffix);
  wifiApOk = WiFi.softAP(apSsid.c_str());
  if (!wifiApOk) {
    Serial.println(F("[WIFI] softAP() failed to start"));
  }
  Serial.print(F("[WIFI] AP started: "));
  Serial.println(apSsid);
  Serial.print(F("[WIFI] IP: "));
  Serial.println(WiFi.softAPIP());

  server.on("/status", handleStatus);
  server.on("/config", HTTP_POST, handleSetConfig);
  server.on("/assign", HTTP_POST, handleAssign);
  server.on("/name", HTTP_POST, handleSetName);
  server.on("/forget", HTTP_POST, handleForget);
  server.begin();
}

// One-line status summary each cycle: level states, No-Power alert
// (with raw pin reading alongside the logical state), and every known
// pump's last CONFIRMED (acked) relay state, labeled P1/P2/... by slot.
void printDebugSummary() {
  Serial.print(F("[STATUS] "));
  for (int lvl = 1; lvl <= numLevels; lvl++) {
    Serial.print(F("L"));
    Serial.print(lvl);
    Serial.print(F(":"));
    Serial.print(inputs[lvl - 1].state ? F("ON") : F("OFF"));
    Serial.print(F(" "));
  }
  Serial.print(F("| NoPower:"));
  Serial.print(inputs[3].state ? F("ON(LOW)") : F("OFF(HIGH)"));
  Serial.print(F(" | "));
  bool any = false;
  for (int i = 0; i < MAX_PUMPS; i++) {
    if (!pumps[i].known) continue;
    any = true;
    Serial.print(F("P"));
    Serial.print(i + 1);
    Serial.print(F("("));
    bool firstLvl = true;
    for (int lvl = 1; lvl <= 3; lvl++) {
      if (pumps[i].assignedLevels & (1 << (lvl - 1))) {
        if (!firstLvl) Serial.print(F(","));
        Serial.print(F("L"));
        Serial.print(lvl);
        firstLvl = false;
      }
    }
    if (firstLvl) Serial.print(F("-"));
    Serial.print(F("):"));
    Serial.print(pumps[i].lastRelayState ? F("ON") : F("OFF"));
    Serial.print(F(" "));
  }
  if (!any) Serial.print(F("(no pumps joined)"));
  Serial.println();
}

void loop() {
  server.handleClient();
  updateWifiLed();
  updateLoraLed();
  updateInputs();
  applyLevelLogic();

  printDebugSummary();
  listenForJoin(JOIN_WINDOW_MS);
  pollCycle();

  delayWithLeds(1000);
}
