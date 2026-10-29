// TH Monitor - Firebase version
// Joins your WiFi and writes readings to a Firebase Realtime Database using its REST API
// (plain HTTPS requests - no Firebase library needed on the board).
//
// Database layout (xxxx = last 4 hex digits of the MAC):
//   /devices/TH-xxxx/latest              {"ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}   overwritten
//   /devices/TH-xxxx/history/<auto id>   {"ts":...,"t":...,"h":...}                        one per reading
//   /devices/TH-xxxx/config/intervalS    60      <- the app can change this; the board reads it
//
// First start: join WiFi "TH-xxxx" (password 12345678) from a phone and pick your WiFi.
// Serial monitor (115200): every reading is printed; "W" forgets the WiFi.
//
// Wiring: DHT22 data D7, DS3231 SDA D3 / SCL D4, blue LED D2 (on while sending).

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <time.h>
#include "firebase_config.h"  // DATABASE_URL - see firebase_config.example.h

const uint8_t DHT_PIN = D7;
const uint8_t SDA_PIN = D3;
const uint8_t SCL_PIN = D4;
const uint8_t LED_PIN = D2;

const char *SETUP_PASSWORD = "12345678";
const long TIMEZONE_OFFSET_S = 5 * 3600 + 30 * 60;  // IST = UTC+5:30

unsigned long intervalS = 60;  // default; the app can change it in /config/intervalS

DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
WiFiClientSecure https;

String deviceId, devicePath;
bool rtcOk = false, clockSynced = false;
unsigned long lastRead = 0;

// ---------- clock ----------

void syncClockFromInternet() {  // called from loop(); copies NTP time into the RTC once
  time_t now = time(nullptr);
  if (clockSynced || !rtcOk || now < 1700000000) return;
  struct tm *t = localtime(&now);
  rtc.adjust(DateTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min,
                      t->tm_sec));
  clockSynced = true;
  Serial.println("Clock set from the internet");
}

void timestamp(char *out) {
  if (!rtcOk) {
    strcpy(out, "unknown");
    return;
  }
  DateTime now = rtc.now();
  snprintf(out, 20, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

// ---------- Firebase REST ----------

// Sends one request to DATABASE_URL + path + ".json". Returns the HTTP status (200 = OK)
// and puts the reply in `reply`.
int firebase(const char *method, const String &path, const String &body, String &reply) {
  HTTPClient http;
  http.begin(https, String(DATABASE_URL) + path + ".json");
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest(method, body);
  reply = http.getString();
  http.end();
  return code;
}

void readAndSend() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  char ts[20];
  timestamp(ts);

  if (isnan(t) || isnan(h)) {
    Serial.printf("%s DHT22 read failed - nothing sent\n", ts);
    return;
  }
  String json = String("{\"ts\":\"") + ts + "\",\"t\":" + String(t, 1) + ",\"h\":" +
                String(h, 1) + "}";
  Serial.print(json);

  digitalWrite(LED_PIN, HIGH);
  String reply;
  int latest = firebase("PUT", devicePath + "/latest", json, reply);    // overwrite
  int history = firebase("POST", devicePath + "/history", json, reply);  // add with a new id
  digitalWrite(LED_PIN, LOW);

  Serial.printf("  -> latest %d, history %d\n", latest, history);
  if (latest != 200) Serial.println("   Firebase said: " + reply);  // e.g. "Permission denied"
}

// The app may change /config/intervalS; pick it up before each reading.
void readConfig() {
  String reply;
  if (firebase("GET", devicePath + "/config/intervalS", "", reply) != 200) return;
  long s = reply.toInt();  // reply is just the number, or "null" if not set yet
  if (s >= 10 && (unsigned long)s != intervalS) {
    intervalS = s;
    Serial.printf("Interval is now %lu s (from Firebase)\n", intervalS);
  } else if (reply == "null") {  // first run: store the default so the app can see it
    firebase("PUT", devicePath + "/config/intervalS", String(intervalS), reply);
  }
}

// ---------- setup & loop ----------

void setup() {
  Serial.begin(115200);
  delay(1000);  // DHT22 needs ~1 s after power-up
  Serial.println("\nTH Monitor - Firebase");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();
  Wire.begin(SDA_PIN, SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) Serial.println("ERROR: DS3231 RTC not found - check SDA/SCL wiring and power");

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  deviceId = "TH-" + mac.substring(8);
  devicePath = "/devices/" + deviceId;

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  Serial.printf("WiFi: connecting (if not set up, join %s / %s from a phone)\n",
                deviceId.c_str(), SETUP_PASSWORD);
  if (!wm.autoConnect(deviceId.c_str(), SETUP_PASSWORD)) {
    Serial.println("No WiFi - restarting");
    ESP.restart();
  }
  Serial.println("WiFi connected, IP " + WiFi.localIP().toString());

  configTime(TIMEZONE_OFFSET_S, 0, "pool.ntp.org", "time.google.com");
  https.setInsecure();  // skip certificate check - fine for learning, not for products
  Serial.println("Writing to " + String(DATABASE_URL) + devicePath);
}

void loop() {
  syncClockFromInternet();

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "W") {
      WiFiManager().resetSettings();
      ESP.restart();
    }
  }

  // Wait up to 10 s for internet time on the first reading, so its timestamp is right.
  if (lastRead == 0 && !clockSynced && millis() < 10000) return;

  if (lastRead == 0 || millis() - lastRead >= intervalS * 1000) {
    lastRead = millis();
    readConfig();
    readAndSend();
  }
}
