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

#define LEVEL_ACTIVE_STATE LOW   // confirmed correct on the bench (switch ON = below level = GND = LOW)

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

void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
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

void initPumpTable() {
  for (int i = 0; i < MAX_PUMPS; i++) pumps[i] = {(uint8_t)i, false, false, false};
  pumps[0].known = true;
}

// --- Level inputs, debounced ---
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

// --- Packet build ---
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

// Sends LEVEL_CMD and waits for CMD_ACK, entirely via the non-blocking
// startTransmit/startReceive + DIO1-interrupt pattern (matching
// lora_ping_pong and the Pump firmware) -- the earlier blocking
// transmit() call was leaving RX-done undetected afterward.
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
        Serial.print(F("[POLL] RX msgType=0x"));
        Serial.print(buf[1], HEX);
        Serial.print(F(" slot="));
        Serial.println(buf[6]);
        if (buf[1] == MSG_CMD_ACK && buf[6] == slot) {
          Serial.print(F("[POLL] CMD_ACK from slot "));
          Serial.println(slot);
          return true;
        }
      } else {
        Serial.print(F("[POLL] readData failed, code "));
        Serial.println(rstate);
      }
      radio.startReceive();
    }
  }
  return false;
}

// --- Simple placeholder level logic ---
bool desiredPumpState[MAX_PUMPS] = { false };

void applyLevelLogic() {
  if (inputs[0].state) desiredPumpState[0] = true;
  if (inputs[1].state) desiredPumpState[0] = false;
}

void pollCycle() {
  static bool everCommanded[MAX_PUMPS] = { false };

  for (int slot = 0; slot < MAX_PUMPS; slot++) {
    if (!pumps[slot].known) continue;

    bool desired = desiredPumpState[slot];

    if (desired && !everCommanded[slot]) delay(STAGGER_MS);

    bool acked = false;
    for (int attempt = 0; attempt <= POLL_RETRIES && !acked; attempt++) {
      acked = pollPump(slot, desired, 2000, POLL_TIMEOUT_MS);
    }

    pumps[slot].online = acked;
    if (acked) {
      pumps[slot].lastRelayState = desired;
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
    pinMode(in.pin, INPUT);
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

  Serial.print(F("[DIAG] IN1(36)="));
  Serial.print(digitalRead(PIN_IN1));
  Serial.print(F(" IN2(39)="));
  Serial.print(digitalRead(PIN_IN2));
  Serial.print(F(" IN3(34)="));
  Serial.print(digitalRead(PIN_IN3));
  Serial.print(F(" IN4(35)="));
  Serial.print(digitalRead(PIN_IN4));
  Serial.print(F(" | debounced state1="));
  Serial.print(inputs[0].state);
  Serial.print(F(" state2="));
  Serial.print(inputs[1].state);
  Serial.print(F(" | desired slot0="));
  Serial.print(desiredPumpState[0]);
  Serial.print(F(" slot1="));
  Serial.println(desiredPumpState[1]);

  pollCycle();

  delay(1000);
}