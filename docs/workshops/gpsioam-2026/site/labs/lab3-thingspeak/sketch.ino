// Lab 3 - Send readings to the cloud (ThingSpeak) and see live charts
// 1. Sign up at thingspeak.mathworks.com -> New Channel -> enable Field 1 (Temp),
//    Field 2 (Humidity), Field 3 (Soil %). API Keys tab -> copy the Write API Key below.
// 2. In Wokwi keep WIFI_SSID "Wokwi-GUEST" (no password). On a real board use the
//    workshop router or a phone hotspot: 2.4 GHz and no login page.
// 3. Free ThingSpeak accounts accept one update every 15 s; we send every 20 s.
//
// Wiring: DHT22 DATA -> GPIO 4 (VCC 3V3, GND GND), soil sensor AOUT -> GPIO 34
// Library: "DHT sensor library for ESPx"

#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h"

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* TS_KEY    = "YOUR_WRITE_API_KEY";

const int DHT_PIN  = 4;
const int SOIL_PIN = 34;
const int DRY = 3000, WET = 1300;   // calibrate for your soil sensor

DHTesp dht;

void setup() {
  Serial.begin(115200);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected, IP " + WiFi.localIP().toString());
}

void loop() {
  TempAndHumidity d = dht.getTempAndHumidity();
  int soil = constrain(map(analogRead(SOIL_PIN), DRY, WET, 0, 100), 0, 100);

  if (dht.getStatus() == 0 && WiFi.status() == WL_CONNECTED) {
    String url = String("http://api.thingspeak.com/update?api_key=") + TS_KEY
               + "&field1=" + String(d.temperature, 1)
               + "&field2=" + String(d.humidity, 1)
               + "&field3=" + soil;
    HTTPClient http;
    http.begin(url);
    int code = http.GET();                 // 200 and an entry number > 0 = success
    Serial.printf("Temp %.1f C  Hum %.1f %%  Soil %d%%  ->  ThingSpeak HTTP %d, entry %s\n",
                  d.temperature, d.humidity, soil, code, http.getString().c_str());
    http.end();
  } else {
    Serial.println("Skipped: sensor read failed or WiFi down");
  }
  delay(20000);
}
