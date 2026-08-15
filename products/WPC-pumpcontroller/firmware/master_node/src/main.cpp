#include <Arduino.h>
#include <RadioLib.h>

// ---------------------------------------------------------------------
// WPC Master Node
// Builds on the verified radio init from firmware/lora_ping_pong.
// Reads 3 water-level float switches + 1 "No Power" input, runs a
// round-robin poll cycle sending LEVEL_CMD to known Pump slots.
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

// Water level / no-power inputs (from Master schematic)
#define PIN_IN1        36   // level switch 1
#define PIN_IN2        39   // level switch 2
#define PIN_IN3        34   // level switch 3
#define PIN_IN4        35   // "No Power" input
#define PIN_IN1_LED    32
#define PIN_IN2_LED    33
#define PIN_IN3_LED    14
#define PIN_IN4_LED    13

// TODO confirm on the bench: does the float switch pull LOW when the
// level is reached, or when clear? Flip this if level logic reads
// inverted once you test with a real switch.
#define LEVEL_ACTIVE_STATE LOW

#define DEBOUNCE_MS        200
#define POLL_TIMEOUT_MS     500
#define POLL_RETRIES        2
#define STAGGER_MS          5000   // gap between starting multiple pumps together

// ---------------------------------------------------------------------
// Protocol
// ---------------------------------------------------------------------
enum MsgType : uint8_t {
  MSG_JOIN_REQUEST = 0x01,
  MSG_JOIN_ACCEPT  = 0x02,
  MSG_LEVEL_CMD    = 0x10,
  MSG_CMD_ACK      = 0x11,
};

#define PROTO_VERSION 1
#define BROADCAST_SLOT 0xFF

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);

volatile bool operationDone = false;
volatile uint32_t isrFireCount = 0;

void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
  isrFireCount++;
}

uint32_t masterId32 = 0;      // lower 4 bytes of this board's MAC
uint8_t  txSeq = 0;

// --- CRC16-CCITT (poly 0x1021, init 0xFFFF) ---
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

// --- Known pumps (placeholder table until the provisioning/app side exists) ---
struct PumpEntry {
  uint8_t slot;
  bool    known;
  bool    lastRelayState;
  bool    online;
};

#define MAX_PUMPS 20
PumpEntry pumps[MAX_PUMPS];

// For now: hardcode slot 0 as the only known pump, so this firmware is
// testable stand-alone against a single Pump Node bench unit. Swap for
// a real join table once JOIN_REQUEST handling + the app exist.
void initPumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) pumps[i] = {(uint8_t)i, false, false, false};
  pumps[0].known = true;
}

// --- Level inputs, debounced ---
struct LevelInput {
  uint8_t pin;
  uint8_t ledPin;
  bool    state;          // debounced logical state (true = active/level reached)
  bool    rawLast;
  uint32_t lastChangeMs;
};

LevelInput inputs[4] = {
  {PIN_IN1, PIN_IN1_LED, false, false, 0},
  {PIN_IN2, PIN_IN2_LED, false, false, 0},
  {PIN_IN3, PIN_IN3_LED, false, false, 0},
  {PIN_IN4, PIN_IN4_LED, false, false, 0},   // no-power input, read but not acted on yet
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

// --- Packet build/send ---
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

void sendLevelCmd(uint8_t slot, bool on) {
  uint8_t payload[1] = { (uint8_t)(on ? 1 : 0) };
  size_t len = buildPacket(MSG_LEVEL_CMD, slot, payload, 1);
  int state = radio.transmit(txPacket, len);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] TX failed, code "));
    Serial.println(state);
  } else {
    Serial.print(F("[POLL] slot "));
    Serial.print(slot);
    Serial.print(F(" LEVEL_CMD -> "));
    Serial.println(on ? F("ON") : F("OFF"));
  }
}

// Uses the same DIO1-interrupt + operationDone flag pattern as the rest
// of the codebase (lora_ping_pong, pump_node) rather than polling a
// packet-length API -- that's how RadioLib's SX126x actually signals
// "a packet arrived."
bool waitForAck(uint8_t expectedSlot, uint32_t timeoutMs) {
  operationDone = false;
  radio.startReceive();
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (operationDone) {
      operationDone = false;
      uint8_t buf[32];
      int len = radio.getPacketLength();
      int state = radio.readData(buf, len);
      if (state == RADIOLIB_ERR_NONE && len >= 10) {
        Serial.print(F("[POLL] RX msgType=0x"));
        Serial.print(buf[1], HEX);
        Serial.print(F(" slot="));
        Serial.println(buf[6]);
        if (buf[1] == MSG_CMD_ACK && buf[6] == expectedSlot) {
          Serial.print(F("[POLL] CMD_ACK from slot "));
          Serial.println(expectedSlot);
          return true;
        }
      } else {
        Serial.print(F("[POLL] readData failed, code "));
        Serial.println(state);
      }
      radio.startReceive();   // stray/unrelated/bad packet -- keep listening
    }
    delay(2);
  }
  return false;
}

// --- Simple placeholder level logic ---
// IN1 active  -> low level  -> pump 0 ON
// IN2 active  -> full level -> pump 0 OFF
// Real per-pump assignment table comes later via the app.
bool desiredPumpState[MAX_PUMPS] = { false };

void applyLevelLogic() {
  if (inputs[0].state) desiredPumpState[0] = true;
  if (inputs[1].state) desiredPumpState[0] = false;
}

void pollCycle() {
  static uint8_t lastCommandedState[MAX_PUMPS] = { 0 };
  static bool everCommanded[MAX_PUMPS] = { false };

  for (int slot = 0; slot < MAX_PUMPS; slot++) {
    if (!pumps[slot].known) continue;

    bool desired = desiredPumpState[slot];

    // stagger only matters when turning multiple pumps ON in the same
    // pass -- with a single known pump today this is a no-op, but the
    // delay is left in place so it's already structured for N pumps.
    if (desired && !everCommanded[slot]) delay(STAGGER_MS);

    bool acked = false;
    for (int attempt = 0; attempt <= POLL_RETRIES && !acked; attempt++) {
      sendLevelCmd(slot, desired);
      acked = waitForAck(slot, POLL_TIMEOUT_MS);
    }

    pumps[slot].online = acked;
    if (acked) {
      pumps[slot].lastRelayState = desired;
      lastCommandedState[slot] = desired;
      everCommanded[slot] = true;
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
    pinMode(in.pin, INPUT);        // external 10k pull-ups already on the board
    pinMode(in.ledPin, OUTPUT);
    digitalWrite(in.ledPin, LOW);
  }

  uint64_t mac = ESP.getEfuseMac();
  masterId32 = (uint32_t)(mac & 0xFFFFFFFF);
  Serial.print(F("[MASTER] ID: 0x"));
  Serial.println(masterId32, HEX);

  initPumpTable();

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
  pollCycle();

  Serial.print(F("[DIAG] isrFireCount="));
  Serial.print(isrFireCount);
  Serial.print(F(" DIO1 pin now="));
  Serial.println(digitalRead(PIN_DIO1));

  delay(1000);   // cycle pacing -- tune once real poll interval is decided (see protocol doc open items)
}
