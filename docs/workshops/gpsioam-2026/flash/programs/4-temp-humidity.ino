// 4 · Temperature & humidity with the time: read the DHT22 every 2 seconds and print
// each reading with the time from the real-time clock (RTC).
// Wiring (ESP32 DevKit V1): DHT22 + -> 3V3, OUT -> D23, - -> GND.  RTC SDA -> D21, SCL -> D22.
// Set the clock by typing in the serial monitor:  T 2026-10-01 10:30:00

#include <Wire.h>
#include <RTClib.h>
#include <DHTesp.h>

const int DHT_PIN = 23;
const int LED = 2;

DHTesp dht;
RTC_DS1307 rtc;  // works with DS3231 and DS1307 (same time registers)
bool rtcOk = false;

String timeNow() {
  if (!rtcOk) return "no clock";
  DateTime n = rtc.now();
  if (n.year() < 2024) return "clock not set";
  char b[20];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
  return b;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Wire.begin(21, 22);
  rtcOk = rtc.begin();
  Serial.println("Program 4: temperature and humidity every 2 seconds, with the time");
  Serial.println(rtcOk ? "Clock found. To set it, type: T 2026-10-01 10:30:00" : "No clock found on D21/D22");
}

void loop() {
  if (Serial.available()) {  // "T 2026-10-01 10:30:00" sets the clock
    String line = Serial.readStringUntil('\n');
    int y, mo, d, h, mi, s;
    if (rtcOk && sscanf(line.c_str(), "T %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
      rtc.adjust(DateTime(y, mo, d, h, mi, s));
      Serial.println("Clock set");
    }
  }

  digitalWrite(LED, HIGH);
  TempAndHumidity r = dht.getTempAndHumidity();
  digitalWrite(LED, LOW);
  if (dht.getStatus() != DHTesp::ERROR_NONE) {
    Serial.println(timeNow() + "  Sensor not found - check the DHT22: data on D23");
  } else {
    Serial.printf("%s  Temperature %.1f C  Humidity %.1f %%\n", timeNow().c_str(), r.temperature, r.humidity);
  }
  delay(2000);
}
