#include <Arduino.h>
#include <RadioLib.h>
#include <Preferences.h>

// ---------------------------------------------------------------------
// WPC Pump Node -- single shared firmware for all Pump Nodes.
// Each board gets a unique default 4-digit Pump ID derived from its own
// MAC address (no per-board firmware differences needed). Joins a
// Master over LoRa via JOIN_REQUEST/JOIN_ACCEPT and is assigned a
// compact wire-protocol slot dynamically.
//
// Defaults (used until the app writes real values into NVS):
//   pumpId    = last 4 digits of this board's MAC (auto-unique)
//   masterId  = DEFAULT_MASTER_ID (today's bench Master)
// Both stored in NVS ("wpc" namespace) so the app can override later.
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
#define PIN_IN4        35
#define PIN_IN1_LED    33
#define PIN_IN4_LED    13
#define PIN_RELAY1     32
#define PIN_PUMP_ON_LED 4

#define INPUT_ACTIVE_STATE LOW

#define FAILSAFE_TIMEOUT_MS 15000UL
#define JOIN_RETRY_MS 1200UL   // faster retry while unjoined, paired with Master's wider listen window

// today's bench Master -- override via app/NVS for a different Master
#define DEFAULT_MASTER_ID 0x86470968UL

enum MsgType : uint8_t {
  MSG_JOIN_REQUEST = 0x01,
  MSG_JOIN_ACCEPT  = 0x02,
  MSG_LEVEL_CMD    = 0x10,
  MSG_CMD_ACK      = 0x11,
};

#define PROTO_VERSION 1

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);
Preferences prefs;

volatile bool operationDone = false;
bool transmitting = false;
uint8_t rxBuf[32];

bool relayState = false;
uint32_t lastCmdMillis = 0;

uint16_t myPumpId = 0;
uint32_t targetMasterId = 0;
bool joined = false;
uint8_t myAssignedSlot = 0xFF;
uint32_t lastJoinAttemptMs = 0;

void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
}

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

void setRelay(bool on) {
  relayState = on;
  digitalWrite(PIN_RELAY1, on ? HIGH : LOW);
  digitalWrite(PIN_PUMP_ON_LED, on ? HIGH : LOW);
  Serial.print(F("[RELAY] -> "));
  Serial.println(on ? F("ON") : F("OFF"));
}

void startReceive() {
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] startReceive failed, code "));
    Serial.println(state);
  }
}

uint8_t txPacket[32];

size_t buildPacket(uint8_t msgType, uint32_t masterId, uint8_t pumpSlot, uint8_t seq,
                    const uint8_t* payload, size_t payloadLen) {
  size_t i = 0;
  txPacket[i++] = PROTO_VERSION;
  txPacket[i++] = msgType;
  txPacket[i++] = (masterId >> 24) & 0xFF;
  txPacket[i++] = (masterId >> 16) & 0xFF;
  txPacket[i++] = (masterId >> 8) & 0xFF;
  txPacket[i++] = masterId & 0xFF;
  txPacket[i++] = pumpSlot;
  txPacket[i++] = seq;
  for (size_t p = 0; p < payloadLen; p++) txPacket[i++] = payload[p];
  uint16_t crc = crc16(txPacket, i);
  txPacket[i++] = (crc >> 8) & 0xFF;
  txPacket[i++] = crc & 0xFF;
  return i;
}

void sendJoinRequest() {
  uint8_t payload[2] = { (uint8_t)(myPumpId >> 8), (uint8_t)(myPumpId & 0xFF) };
  size_t len = buildPacket(MSG_JOIN_REQUEST, targetMasterId, 0xFF, 0, payload, 2);
  transmitting = true;
  int state = radio.startTransmit(txPacket, len);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] JOIN_REQUEST TX failed, code "));
    Serial.println(state);
    transmitting = false;
    startReceive();
  } else {
    Serial.print(F("[JOIN] requesting join, pumpId="));
    Serial.println(myPumpId);
  }
}

void sendCmdAck(uint32_t masterId, uint8_t seqEcho) {
  bool in1 = (digitalRead(PIN_IN1) == INPUT_ACTIVE_STATE);
  bool in4 = (digitalRead(PIN_IN4) == INPUT_ACTIVE_STATE);
  uint8_t payload[3] = { (uint8_t)(relayState ? 1 : 0), (uint8_t)(in1 ? 1 : 0), (uint8_t)(in4 ? 1 : 0) };
  size_t len = buildPacket(MSG_CMD_ACK, masterId, myAssignedSlot, seqEcho, payload, 3);

  transmitting = true;
  int state = radio.startTransmit(txPacket, len);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] CMD_ACK TX failed, code "));
    Serial.println(state);
    transmitting = false;
    startReceive();
  } else {
    Serial.println(F("[LoRa] CMD_ACK sent"));
  }
}

void handlePacket(const uint8_t* buf, int len) {
  if (len < 10) return;
  uint16_t rxCrc = (buf[len - 2] << 8) | buf[len - 1];
  if (crc16(buf, len - 2) != rxCrc) {
    Serial.println(F("[LoRa] CRC mismatch, dropped"));
    return;
  }

  uint8_t msgType = buf[1];
  uint32_t masterId = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) |
                       ((uint32_t)buf[4] << 8) | buf[5];
  uint8_t pumpSlot = buf[6];
  uint8_t seq = buf[7];

  if (masterId != targetMasterId) return;

  if (!joined) {
    if (msgType == MSG_JOIN_ACCEPT) {
      // JOIN_ACCEPT is broadcast -- verify it was actually meant for
      // THIS pump before accepting it, or any pump still unjoined at
      // the same moment would grab someone else's slot assignment.
      uint16_t acceptedPumpId = ((uint16_t)buf[8] << 8) | buf[9];
      if (acceptedPumpId != myPumpId) return;
      myAssignedSlot = buf[10];
      joined = true;
      lastCmdMillis = millis();
      Serial.print(F("[JOIN] accepted, assigned slot "));
      Serial.println(myAssignedSlot);
    }
    return;
  }

  if (msgType != MSG_LEVEL_CMD || pumpSlot != myAssignedSlot) return;

  bool desired = (buf[8] != 0);
  lastCmdMillis = millis();
  setRelay(desired);
  sendCmdAck(masterId, seq);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_IN1, INPUT);
  pinMode(PIN_IN4, INPUT);
  pinMode(PIN_IN1_LED, OUTPUT);
  pinMode(PIN_IN4_LED, OUTPUT);
  pinMode(PIN_RELAY1, OUTPUT);
  pinMode(PIN_PUMP_ON_LED, OUTPUT);
  digitalWrite(PIN_RELAY1, LOW);
  digitalWrite(PIN_PUMP_ON_LED, LOW);

  prefs.begin("wpc", false);
  uint64_t mac = ESP.getEfuseMac();
  uint16_t defaultPumpId = (uint16_t)(mac % 10000);
  myPumpId = prefs.getUShort("pumpId", defaultPumpId);
  targetMasterId = prefs.getULong("masterId", DEFAULT_MASTER_ID);

  Serial.print(F("[PUMP] pumpId="));
  Serial.print(myPumpId);
  Serial.print(F(" targetMasterId=0x"));
  Serial.println(targetMasterId, HEX);

  loraSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                           LORA_SYNCWORD, LORA_TXPOWER, 8, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] radio.begin() failed, code "));
    Serial.println(state);
    while (true) delay(1000);
  }
  radio.setDio1Action(onRadioAction);

  startReceive();
  lastJoinAttemptMs = 0;
}

void loop() {
  if (operationDone) {
    operationDone = false;

    if (transmitting) {
      transmitting = false;
      startReceive();
    } else {
      int len = radio.getPacketLength();
      if (len > 0) {
        int state = radio.readData(rxBuf, len);
        if (state == RADIOLIB_ERR_NONE) {
          handlePacket(rxBuf, len);
        } else {
          Serial.print(F("[LoRa] readData failed, code "));
          Serial.println(state);
          startReceive();
        }
      } else {
        startReceive();
      }
    }
  }

  if (!joined) {
    if (millis() - lastJoinAttemptMs > JOIN_RETRY_MS) {
      lastJoinAttemptMs = millis();
      sendJoinRequest();
    }
    return;
  }

  digitalWrite(PIN_IN1_LED, (digitalRead(PIN_IN1) == INPUT_ACTIVE_STATE) ? HIGH : LOW);
  digitalWrite(PIN_IN4_LED, (digitalRead(PIN_IN4) == INPUT_ACTIVE_STATE) ? HIGH : LOW);

  // Fail-safe applies regardless of relay state -- an idle (OFF) pump
  // whose Master forgot it (e.g. Master reboot losing its RAM-only join
  // table) would otherwise sit silently orphaned forever, since it was
  // never actively driving anything for the old relayState-only check
  // to catch. Losing contact for too long, in ANY state, means: cut
  // the relay if it happened to be on, and go back to sending
  // JOIN_REQUEST so it can automatically recover once the Master is
  // back and listening again -- no manual reset needed on this side.
  if (millis() - lastCmdMillis > FAILSAFE_TIMEOUT_MS) {
    if (relayState) {
      Serial.println(F("[FAILSAFE] no LEVEL_CMD received in time -- forcing OFF"));
      setRelay(false);
    }
    Serial.println(F("[FAILSAFE] lost contact with Master -- rejoining"));
    joined = false;
    myAssignedSlot = 0xFF;
    lastJoinAttemptMs = 0;
  }
}
