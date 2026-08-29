#include <Arduino.h>
#include <RadioLib.h>

#ifndef BOARD_ROLE
#define BOARD_ROLE   1
#endif
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
#define PIN_LED    2

SPIClass loraSPI(HSPI);
SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_RESET, PIN_BUSY, loraSPI);

volatile bool operationDone = false;
bool transmitting = false;
uint32_t counter = 0;
char txBuf[32];

#define RETRY_TIMEOUT_MS 5000
bool waitingForReply = false;
uint32_t lastActionMs = 0;

void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
}

void blink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(onMs);
    digitalWrite(PIN_LED, LOW);
    if (i < times - 1) delay(offMs);
  }
}

void haltWithError(const char* msg, int code) {
  Serial.print(F("[LoRa] FATAL: "));
  Serial.print(msg);
  Serial.print(F(" (code "));
  Serial.print(code);
  Serial.println(F(")"));
  while (true) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    delay(100);
  }
}

void startReceive() {
  int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] startReceive failed, code "));
    Serial.println(state);
  }
}

void sendPacket() {
  snprintf(txBuf, sizeof(txBuf), "%s:%lu",
           (BOARD_ROLE == 0) ? "PING" : "PONG",
           (unsigned long)counter);

  transmitting = true;
  int state = radio.startTransmit(txBuf);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print(F("[LoRa] startTransmit failed, code "));
    Serial.println(state);
    transmitting = false;
    startReceive();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  uint64_t mac = ESP.getEfuseMac();
  Serial.println();
  Serial.print(F("[ID] Board ID: 0x"));
  Serial.println((uint32_t)(mac & 0xFFFFFFFF), HEX);

#if BOARD_ROLE == 2
  Serial.println(F("[IDLE] Parked -- no radio init, zero RF activity. Self-test then solid ON."));
  blink(3, 150, 150);
  digitalWrite(PIN_LED, HIGH);
  return;   // never touch the radio at all -- guarantees no interference with a real test elsewhere
#endif

  Serial.print(F("[LoRa] Booting as "));
  Serial.println(BOARD_ROLE == 0 ? F("INITIATOR (PING)") : F("RESPONDER (PONG)"));

  loraSPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);

  int state = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                           LORA_SYNCWORD, LORA_TXPOWER, 8, 0, false);
  if (state != RADIOLIB_ERR_NONE) {
    haltWithError("radio.begin() failed - check wiring/power", state);
  }

  radio.setDio1Action(onRadioAction);

  Serial.println(F("[LoRa] Radio initialized OK."));
  blink(3, 80, 80);

  if (BOARD_ROLE == 0) {
    delay(2000);
    sendPacket();
    waitingForReply = true;
    lastActionMs = millis();
  } else {
    startReceive();
  }
}

void loop() {
#if BOARD_ROLE == 2
  return;   // idle -- nothing to ever do
#endif
  // PING-side retry -- resend the SAME packet (counter unchanged) if no
  // reply has arrived within RETRY_TIMEOUT_MS. Without this, a single
  // dropped first packet (normal, occasional on any real RF link) makes
  // both sides wait forever with no way to recover, which looks
  // identical to a genuinely dead radio from the outside.
  if (BOARD_ROLE == 0 && waitingForReply && !transmitting &&
      (millis() - lastActionMs > RETRY_TIMEOUT_MS)) {
    Serial.println(F("[LoRa] No reply -- retrying same packet"));
    sendPacket();
    lastActionMs = millis();
  }

  if (!operationDone) return;
  operationDone = false;

  if (transmitting) {
    transmitting = false;
    Serial.print(F("[LoRa] TX done: "));
    Serial.println(txBuf);
    blink(1, 50, 0);

    startReceive();
    if (BOARD_ROLE == 0) {
      waitingForReply = true;
      lastActionMs = millis();
    }

  } else {
    String received;
    int state = radio.readData(received);

    if (state == RADIOLIB_ERR_NONE) {
      float rssi = radio.getRSSI();
      float snr  = radio.getSNR();

      Serial.print(F("[LoRa] RX: \""));
      Serial.print(received);
      Serial.print(F("\"  RSSI: "));
      Serial.print(rssi);
      Serial.print(F(" dBm  SNR: "));
      Serial.print(snr);
      Serial.println(F(" dB"));

      blink(2, 50, 80);

      waitingForReply = false;
      counter++;
      delay(500);
      sendPacket();
      if (BOARD_ROLE == 0) {
        waitingForReply = true;
        lastActionMs = millis();
      }

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println(F("[LoRa] RX CRC error"));
      startReceive();
    } else {
      Serial.print(F("[LoRa] readData failed, code "));
      Serial.println(state);
      startReceive();
    }
  }
}
