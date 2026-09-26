// Lab 1 - First ESP32: blink an LED and read temperature & humidity (DHT22)
// Board: ESP32 DevKit V1 (30-pin) or Wokwi ESP32 DevKitC. Serial Monitor: 115200.
// Library: "DHT sensor library for ESPx" (by beegee_tokyo)
//
// Wiring
//   DHT22 VCC  -> 3V3
//   DHT22 DATA -> GPIO 4
//   DHT22 GND  -> GND
//   LED (+ 220 ohm resistor) -> GPIO 2  (the DevKit V1 also has an on-board LED on GPIO 2)

#include "DHTesp.h"

const int DHT_PIN = 4;
const int LED_PIN = 2;

DHTesp dht;

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);   // use DHTesp::DHT11 if you have a DHT11
  Serial.println("Lab 1: DHT22 ready");
}

void loop() {
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));   // blink once per reading

  TempAndHumidity d = dht.getTempAndHumidity();
  if (dht.getStatus() != 0) {
    Serial.println(String("Sensor read failed: ") + dht.getStatusString());
  } else {
    Serial.printf("Temp %.1f C   Humidity %.1f %%\n", d.temperature, d.humidity);
  }
  delay(2000);   // DHT22 needs at least 2 s between readings
}
