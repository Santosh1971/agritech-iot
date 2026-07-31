#include <Arduino.h>
#include <RadioLib.h>

#define BOARD_ROLE   1
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

  Serial.println();
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
  } else {
    startReceive();
  }
}

void loop() {
  if (!operationDone) return;
  operationDone = false;

  if (transmitting) {
    transmitting = false;
    Serial.print(F("[LoRa] TX done: "));
    Serial.println(txBuf);
    blink(1, 50, 0);

    startReceive();

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

      counter++;
      delay(500);
      sendPacket();

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
