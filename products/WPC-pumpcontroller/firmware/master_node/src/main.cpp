#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>

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
uint16_t storedPumpIds[MAX_PUMPS];   // NVS-backed slot->pumpId mapping, 0 = empty

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
  bool     lastRelayState;
  bool     online;
};
PumpEntry pumps[MAX_PUMPS];

void initPumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) pumps[i] = {(uint8_t)i, false, 0, false, false};
}

void savePumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) storedPumpIds[i] = pumps[i].known ? pumps[i].pumpId : 0;
  prefs.putBytes("pumpTable", storedPumpIds, sizeof(storedPumpIds));
}

void loadPumpTable() {
  size_t n = prefs.getBytes("pumpTable", storedPumpIds, sizeof(storedPumpIds));
  if (n != sizeof(storedPumpIds)) {
    memset(storedPumpIds, 0, sizeof(storedPumpIds));
    return;
  }
  for (int i = 0; i < MAX_PUMPS; i++) {
    if (storedPumpIds[i] != 0) {
      pumps[i].known = true;
      pumps[i].pumpId = storedPumpIds[i];
      Serial.print(F("[NVS] restored slot "));
      Serial.print(i);
      Serial.print(F(" -> pumpId "));
      Serial.println(storedPumpIds[i]);
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

void updateInputs() {
  uint32_t now = millis();
  for (auto& in : inputs) {
    bool raw = (digitalRead(in.pin) == LEVEL_ACTIVE_STATE);
    if (raw != in.rawLast) {
      in.lastChangeMs = now;
      in.rawLast = raw;
    }
    if ((now - in.lastChangeMs) > DEBOUNCE_MS && in.state != raw) {
      in.state = raw;
      digitalWrite(in.ledPin, in.state ? HIGH : LOW);
      Serial.print(F("[LEVEL] pin "));
      Serial.print(in.pin);
      Serial.print(F(" -> "));
      Serial.println(in.state ? F("ACTIVE") : F("CLEAR"));
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
  desiredPumpState[0] = inputs[0].state;   // IN1 -> whichever pump joined first (slot 0)
  desiredPumpState[1] = inputs[1].state;   // IN2 -> whichever pump joined second (slot 1)
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
}

void loop() {
  updateInputs();
  applyLevelLogic();

  listenForJoin(JOIN_WINDOW_MS);
  pollCycle();

  delay(1000);
}
