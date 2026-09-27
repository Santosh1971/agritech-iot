// TH Monitor - Level 0 baseline
// Reads temperature & humidity from a DHT22 and the time from a DS3231 RTC,
// then prints one JSON line on the serial port every READ_INTERVAL_S seconds:
//   {"ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}
//
// To set the clock, send this on the serial monitor:  T 2026-10-01 10:30:00
//
// Board: Wemos D1 mini (ESP8266). Serial monitor: 115200 baud.
//
// Wiring on the TH monitor PCB:
//   DHT22 data  -> D7 (GPIO13)
//   DS3231 SDA  -> D3 (GPIO0)
//   DS3231 SCL  -> D4 (GPIO2)
//   Blue LED    -> D2 (GPIO4)

#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>

const uint8_t DHT_PIN = D7;
const uint8_t SDA_PIN = D3;
const uint8_t SCL_PIN = D4;
const uint8_t LED_PIN = D2;

const unsigned long READ_INTERVAL_S = 10;  // how often to read, in seconds

DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
bool rtcOk = false;
unsigned long lastRead = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);  // DHT22 needs ~1 s after power-up
  Serial.println("\nTH Monitor - Level 0");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  Wire.begin(SDA_PIN, SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("ERROR: DS3231 RTC not found - check SDA/SCL wiring and power");
  } else if (rtc.lostPower() || rtc.now().year() < 2024) {
    // RTC battery was removed or never set: use the time this sketch was compiled.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC time was not set - set it to the compile time");
  }
}

// Handle "T YYYY-MM-DD HH:MM:SS" typed on the serial monitor: set the RTC clock.
void checkSerialCommand() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  int y, mo, d, h, mi, se;
  if (line.startsWith("T ") &&
      sscanf(line.c_str() + 2, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6) {
    rtc.adjust(DateTime(y, mo, d, h, mi, se));
    Serial.println("RTC time set");
  } else if (line.length()) {
    Serial.println("Unknown command. To set the clock: T 2026-10-01 10:30:00");
  }
}

void readAndPrint() {
  digitalWrite(LED_PIN, HIGH);  // LED on while reading

  float t = dht.readTemperature();  // degrees C
  float h = dht.readHumidity();     // % RH

  char ts[20] = "unknown";
  if (rtcOk) {
    DateTime now = rtc.now();
    snprintf(ts, sizeof(ts), "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(),
             now.day(), now.hour(), now.minute(), now.second());
  }

  if (isnan(t) || isnan(h)) {
    Serial.printf("{\"ts\":\"%s\",\"error\":\"DHT22 read failed\"}\n", ts);
  } else {
    Serial.printf("{\"ts\":\"%s\",\"t\":%.1f,\"h\":%.1f}\n", ts, t, h);
  }

  digitalWrite(LED_PIN, LOW);
}

void loop() {
  checkSerialCommand();

  // Read every READ_INTERVAL_S seconds without blocking, so serial commands still work.
  if (lastRead == 0 || millis() - lastRead >= READ_INTERVAL_S * 1000) {
    lastRead = millis();
    readAndPrint();
  }
}
