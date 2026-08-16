#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

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

#define LEVEL_ACTIVE_STATE LOW

#define DEBOUNCE_MS        200
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
WebServer server(80);   // NVS-backed slot->pumpId mapping, 0 = empty

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
};
PumpEntry pumps[MAX_PUMPS];

uint8_t numLevels = 3;   // configurable 1-3, persisted in NVS

struct StoredPump { uint16_t pumpId; uint8_t levelMask; char name[16]; };
StoredPump storedPumps[MAX_PUMPS];

void initPumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) {
    pumps[i] = {(uint8_t)i, false, 0, 0, "", false, false};
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

void updateInputs() {
  uint32_t now = millis();
  for (int i = 0; i < 4; i++) {
    LevelInput& in = inputs[i];
    bool raw = (digitalRead(in.pin) == LEVEL_ACTIVE_STATE);
    if (raw != in.rawLast) {
      in.lastChangeMs = now;
      in.rawLast = raw;
    }
    if ((now - in.lastChangeMs) > DEBOUNCE_MS && in.state != raw) {
      in.state = raw;
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
    if (operationDone) {
      operationDone = false;
      uint8_t buf[32];
      int len = radio.getPacketLength();
      int state = radio.readData(buf, len);
      if (state == RADIOLIB_ERR_NONE && len >= 10) {
        uint16_t rxCrc = (buf[len - 2] << 8) | buf[len - 1];
        if (crc16(buf, len - 2) == rxCrc && buf[1] == MSG_JOIN_REQUEST) {
          uint16_t pumpId = ((uint16_t)buf[8] << 8) | buf[9];
          int slot = findSlotByPumpId(pumpId);
          if (slot < 0) slot = findFreeSlot();
          if (slot >= 0) {
            pumps[slot].known = true;
            pumps[slot].pumpId = pumpId;
            savePumpTable();
            Serial.print(F("[JOIN] pumpId "));
            Serial.print(pumpId);
            Serial.print(F(" -> slot "));
            Serial.println(slot);

            // Echo the accepted pumpId back in the payload -- JOIN_ACCEPT
            // is broadcast over LoRa, so any other unjoined pump hearing
            // it must be able to tell it wasn't addressed to them.
            uint8_t payload[3] = { (uint8_t)(pumpId >> 8), (uint8_t)(pumpId & 0xFF), (uint8_t)slot };
            size_t alen = buildPacket(MSG_JOIN_ACCEPT, (uint8_t)slot, payload, 3);
            operationDone = false;
            radio.startTransmit(txPacket, alen);
            uint32_t t0 = millis();
            while (!operationDone && millis() - t0 < 1000) {}
            operationDone = false;
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

  radio.startReceive();
  uint32_t start = millis();
  while (millis() - start < rxTimeoutMs) {
    server.handleClient();
    if (operationDone) {
      operationDone = false;
      uint8_t buf[32];
      int rlen = radio.getPacketLength();
      int rstate = radio.readData(buf, rlen);
      if (rstate == RADIOLIB_ERR_NONE && rlen >= 10) {
        if (buf[1] == MSG_CMD_ACK && buf[6] == slot) {
          Serial.print(F("[POLL] CMD_ACK from slot "));
          Serial.println(slot);
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
      if ((pumps[slot].assignedLevels & (1 << (lvl - 1))) && inputs[lvl - 1].state) {
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

  for (int slot = 0; slot < MAX_PUMPS; slot++) {
    if (!pumps[slot].known) continue;

    bool desired = desiredPumpState[slot];
    bool turningOn = desired && !wasOn[slot];

    // stagger only between MULTIPLE pumps freshly turning ON in the
    // same pass -- the first one in a pass never waits
    if (turningOn && freshOnCountThisPass > 0) delay(STAGGER_MS);
    if (turningOn) freshOnCountThisPass++;

    bool acked = false;
    for (int attempt = 0; attempt <= POLL_RETRIES && !acked; attempt++) {
      acked = pollPump(slot, desired, 2000, POLL_TIMEOUT_MS);
    }

    pumps[slot].online = acked;
    if (acked) {
      pumps[slot].lastRelayState = desired;
      wasOn[slot] = desired;
    } else {
      Serial.print(F("[POLL] slot "));
      Serial.print(slot);
      Serial.println(F(" NOT ACKED -- marked offline"));
    }
  }
}

// GET /status -- JSON snapshot of sump levels + known pumps, for the
// app to poll while connected to this Master's SoftAP.
void handleStatus() {
  JsonDocument doc;
  char idbuf[12];
  snprintf(idbuf, sizeof(idbuf), "0x%08X", masterId32);
  doc["masterId"] = idbuf;
  doc["numLevels"] = numLevels;

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

void setup() {
  Serial.begin(115200);
  delay(500);

  for (auto& in : inputs) {
    pinMode(in.pin, INPUT);
    pinMode(in.ledPin, OUTPUT);
    digitalWrite(in.ledPin, LOW);
  }

  uint64_t mac = ESP.getEfuseMac();
  masterId32 = (uint32_t)(mac & 0xFFFFFFFF);
  Serial.print(F("[MASTER] ID: 0x"));
  Serial.println(masterId32, HEX);

  prefs.begin("wpc", false);
  numLevels = prefs.getUChar("numLevels", 3);
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
  String apSsid = "WPC-Master-" + String(masterId32 & 0xFFFF, HEX);
  WiFi.softAP(apSsid.c_str());
  Serial.print(F("[WIFI] AP started: "));
  Serial.println(apSsid);
  Serial.print(F("[WIFI] IP: "));
  Serial.println(WiFi.softAPIP());

  server.on("/status", handleStatus);
  server.on("/config", HTTP_POST, handleSetConfig);
  server.on("/assign", HTTP_POST, handleAssign);
  server.on("/name", HTTP_POST, handleSetName);
  server.begin();
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

void loop() {
  server.handleClient();
  updateInputs();
  applyLevelLogic();

  listenForJoin(JOIN_WINDOW_MS);
  pollCycle();

  delay(1000);
}
