#include <Arduino.h>

// Minimal, isolated ADC diagnostic -- no WiFi, no LoRa, no relay, no join
// logic. Just reads IN1 (GPIO36) and IN4 (GPIO35) and prints raw counts
// plus ESP32's own eFuse-calibrated millivolt conversion, continuously.
// Purpose: rule out any interference from the rest of the Pump Node
// firmware (radio init, SoftAP, etc.) on these two ADC channels.

#define PIN_IN1 36
#define PIN_IN4 35

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("=== ADC-only test: IN1 (GPIO36) / IN4 (GPIO35) ==="));
  Serial.println(F("No WiFi, no LoRa, no relay -- just analogRead() in a loop."));
}

void loop() {
  int in1Raw = analogRead(PIN_IN1);
  int in4Raw = analogRead(PIN_IN4);
  uint32_t in1mV = analogReadMilliVolts(PIN_IN1);
  uint32_t in4mV = analogReadMilliVolts(PIN_IN4);

  Serial.print(F("IN1 raw="));
  Serial.print(in1Raw);
  Serial.print(F(" ("));
  Serial.print(in1mV);
  Serial.print(F("mV)   IN4 raw="));
  Serial.print(in4Raw);
  Serial.print(F(" ("));
  Serial.print(in4mV);
  Serial.println(F("mV)"));

  delay(300);
}
