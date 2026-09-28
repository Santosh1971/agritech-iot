// 4 · Temperature & humidity: read a DHT22 every 2 seconds and print it.
// Wiring: DHT22 + (VCC) -> 3V3, OUT (data) -> GPIO 4, - (GND) -> GND.
// The blue LED blinks on every reading.

#include <DHTesp.h>

const int DHT_PIN = 4;
const int LED = 2;

DHTesp dht;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.println("Program 4: temperature and humidity every 2 seconds");
}

void loop() {
  digitalWrite(LED, HIGH);
  TempAndHumidity r = dht.getTempAndHumidity();
  digitalWrite(LED, LOW);

  if (dht.getStatus() != DHTesp::ERROR_NONE) {
    Serial.printf("Sensor not found (%s) - check the DHT22 wiring: data on GPIO 4\n", dht.getStatusString());
  } else {
    Serial.printf("Temperature %.1f C   Humidity %.1f %%\n", r.temperature, r.humidity);
  }
  delay(2000);
}
