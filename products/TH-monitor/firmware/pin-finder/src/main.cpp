// Pin-finder for the Wemos D1 mini TH monitor.
// 1) Tries every I2C SDA/SCL pair among the candidate pins and lists devices found
//    (DS3231 = 0x68, its EEPROM = 0x57, IP5306 = 0x75).
// 2) Tries a DHT22 read on every candidate pin.
#include <Arduino.h>
#include <Wire.h>
#include <DHTesp.h>

struct Pin { const char *name; uint8_t gpio; };
const Pin PINS[] = {{"D0", 16}, {"D1", 5}, {"D2", 4}, {"D3", 0}, {"D4", 2}, {"D5", 14},
                    {"D6", 12}, {"D7", 13}, {"D8", 15}, {"RX", 3}};
const int N = sizeof(PINS) / sizeof(PINS[0]);

void scanI2C() {
  Serial.println("\n== I2C scan ==");
  // An idle I2C line is pulled HIGH. Pins that read LOW can't be SDA/SCL (and would hang the scan).
  bool idleHigh[N];
  for (int i = 0; i < N; i++) {
    pinMode(PINS[i].gpio, INPUT_PULLUP);
    delay(2);
    idleHigh[i] = digitalRead(PINS[i].gpio);
    Serial.printf("%s idle=%s\n", PINS[i].name, idleHigh[i] ? "HIGH" : "LOW");
  }
  for (int s = 0; s < N; s++) {
    for (int c = 0; c < N; c++) {
      if (s == c || !idleHigh[s] || !idleHigh[c]) continue;
      Serial.printf("try SDA=%s SCL=%s\n", PINS[s].name, PINS[c].name);
      Serial.flush();
      Wire.begin(PINS[s].gpio, PINS[c].gpio);
      Wire.setClockStretchLimit(2000);  // us; don't wait forever on a held-low SCL
      String found;
      for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        yield();
        if (Wire.endTransmission() == 0) found += String(" 0x") + String(a, HEX);
      }
      if (found.length())
        Serial.printf("SDA=%s SCL=%s ->%s\n", PINS[s].name, PINS[c].name, found.c_str());
      yield();
    }
  }
}

void scanDHT() {
  Serial.println("\n== DHT scan (DHT22 and DHT11 timing) ==");
  const DHTesp::DHT_MODEL_t models[] = {DHTesp::DHT22, DHTesp::DHT11};
  for (int i = 0; i < N; i++) {
    for (auto model : models) {
      DHTesp dht;
      dht.setup(PINS[i].gpio, model);
      delay(2100);  // DHT needs ~2 s between reads
      TempAndHumidity v = dht.getTempAndHumidity();
      Serial.printf("%s (GPIO%d) %s: %s  T=%.1f H=%.1f\n", PINS[i].name, PINS[i].gpio,
                    model == DHTesp::DHT22 ? "DHT22" : "DHT11",
                    dht.getStatus() == DHTesp::ERROR_NONE ? "OK  " : dht.getStatusString(),
                    v.temperature, v.humidity);
    }
  }
}

void setup() {
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);  // frees RX (GPIO3) for the DHT scan
  delay(500);
  Serial.println("\n\nTH pin-finder");
  scanI2C();
  scanDHT();
  Serial.println("\n== done ==");
}

void loop() {}
