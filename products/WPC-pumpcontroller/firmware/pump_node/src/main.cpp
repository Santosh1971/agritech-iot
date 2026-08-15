#include <Arduino.h>
#include <RadioLib.h>

// ---------------------------------------------------------------------
// WPC Pump Node
// Pins confirmed against hand-drawn bench schematic (15 Aug) + formal
// PCB schematic + Master's working lora_ping_pong pinout.
// LoRa section is identical to Master.
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

#define PIN_IN1        36   // flow / pump-status input (digital on the bench for now)
#define PIN_IN4        35   // "No Power" input
#define PIN_IN1_LED    33
#define PIN_IN4_LED    13
#define PIN_RELAY1     32   // drives ULN2003 -> relay on the real board; an LED on the bench
#define PIN_PUMP_ON_LED 4

// TODO: confirm polarity once real flow/status wiring exists -- same
// convention as Master for now (short to GND = active).
#define INPUT_ACTIVE_STATE LOW

// Until provisioning/JOIN exists, this pump answers as slot 0 and
// accepts LEVEL_CMD from whichever Master addresses that slot --
// there's only one Master on the bench, so this is safe for now.
#define MY_SLOT 0

#define FAILSAFE_TIMEOUT_MS 15000UL   // no LEVEL_CMD in this long -> force OFF locally

// ---------------------------------------------------------------------
enum MsgType : uint8_t {
  MSG_JOIN_REQUEST = 0x01,
  MSG_JOIN_ACCEPT  = 0x02,
  MSG_LEVEL_CMD    = 0x10,
  MSG_CMD_ACK      = 0x11,
};

#define PROTO_VERSION 1

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);

volatile bool operationDone = false;
bool transmitting = false;
uint8_t rxBuf[32];

bool relayState = false;
uint32_t lastCmdMillis = 0;
bool everReceivedCmd = false;

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

void sendCmdAck(uint32_t masterId, uint8_t seqEcho) {
  bool in1 = (digitalRead(PIN_IN1) == INPUT_ACTIVE_STATE);
  bool in4 = (digitalRead(PIN_IN4) == INPUT_ACTIVE_STATE);

  size_t i = 0;
  txPacket[i++] = PROTO_VERSION;
  txPacket[i++] = MSG_CMD_ACK;
  txPacket[i++] = (masterId >> 24) & 0xFF;
  txPacket[i++] = (masterId >> 16) & 0xFF;
  txPacket[i++] = (masterId >> 8) & 0xFF;
  txPacket[i++] = masterId & 0xFF;
  txPacket[i++] = MY_SLOT;
  txPacket[i++] = seqEcho;
  txPacket[i++] = relayState ? 1 : 0;
  txPacket[i++] = in1 ? 1 : 0;
  txPacket[i++] = in4 ? 1 : 0;
  uint16_t crc = crc16(txPacket, i);
  txPacket[i++] = (crc >> 8) & 0xFF;
  txPacket[i++] = crc & 0xFF;

  transmitting = true;
  int state = radio.startTransmit(txPacket, i);
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

  if (msgType != MSG_LEVEL_CMD || pumpSlot != MY_SLOT) return;

  bool desired = (buf[8] != 0);

  if (!everReceivedCmd) {
    Serial.print(F("[PUMP] first command from Master ID 0x"));
    Serial.println(masterId, HEX);
    everReceivedCmd = true;
  }

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

  loraSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                           LORA_SYNCWORD, LORA_TXPOWER, 8, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] radio.begin() failed, code "));
    Serial.println(state);
    while (true) delay(1000);
  }
  radio.setDio1Action(onRadioAction);

  Serial.print(F("[PUMP] slot "));
  Serial.print(MY_SLOT);
  Serial.println(F(" ready, listening..."));

  startReceive();
  lastCmdMillis = millis();   // don't fail-safe immediately on boot
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
        }
      }
      startReceive();
    }
  }

  // mirror IN1/IN4 straight to their LEDs for bench visibility
  digitalWrite(PIN_IN1_LED, (digitalRead(PIN_IN1) == INPUT_ACTIVE_STATE) ? HIGH : LOW);
  digitalWrite(PIN_IN4_LED, (digitalRead(PIN_IN4) == INPUT_ACTIVE_STATE) ? HIGH : LOW);

  // fail-safe: force OFF if the Master goes quiet
  if (everReceivedCmd && relayState && (millis() - lastCmdMillis > FAILSAFE_TIMEOUT_MS)) {
    Serial.println(F("[FAILSAFE] no LEVEL_CMD received in time -- forcing OFF"));
    setRelay(false);
  }
}
