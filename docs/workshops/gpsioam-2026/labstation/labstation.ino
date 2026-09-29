// Lab Station: the workshop ESP32 kit gifted to the college.
//
// - Logs temperature, humidity and water flow every 5 minutes (with the RTC time) into its
//   own flash, and uploads the log to agrisenseandcontrol.in whenever it is online.
// - Publishes live readings and takes PUMP ON / PUMP OFF over MQTT, exactly like workshop
//   program 9, so the students' installed farm apps keep working.
// - Checks the server every minute for a firmware update (a new Lab Station version, or a
//   student's program sent from the workshop flasher) and installs it over WiFi.
// - WiFi: the college WiFi, else the fixed update hotspot. To set the college WiFi, hold BOOT
//   for 5 seconds (a setup network appears), or type in the serial monitor:  WIFI name,password
//
// Restore it any time over USB: workshop flasher -> "Lab Station" ready program.
// Wiring (ESP32 DevKit V1): DHT22 D23, flow sensor D35 (via divider), relay D19 (HIGH = ON),
// RTC SDA D21 / SCL D22, blue LED GPIO 2, BOOT GPIO 0.

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHTesp.h>
#include <time.h>

// Secrets are written into lab_secrets.h on the server at build time (never in the repo).
#if __has_include("lab_secrets.h")
#include "lab_secrets.h"
#endif
#ifndef LAB_TOKEN
#define LAB_TOKEN ""
#endif
#ifndef LAB_HOTSPOT_SSID
#define LAB_HOTSPOT_SSID "AgriSense-Lab"
#endif
#ifndef LAB_HOTSPOT_PASS
#define LAB_HOTSPOT_PASS ""
#endif
#ifndef LAB_VERSION
#define LAB_VERSION "dev"
#endif
#define LAB_SERVER "https://agrisenseandcontrol.in"

const int DHT_PIN = 23, FLOW_PIN = 35, RELAY = 19, BUTTON = 0, LED = 2;
const bool RELAY_ACTIVE_LOW = false;
const unsigned long LOG_EVERY_MS = 5UL * 60 * 1000;
const unsigned long CHECKIN_EVERY_MS = 60UL * 1000;
const unsigned long LIVE_EVERY_MS = 10UL * 1000;
const unsigned long NO_FLOW_STOP_MS = 10000;
const uint32_t MIN_PULSES = 5;
const float PULSES_PER_LITRE = 450.0;
const long IST_OFFSET_S = 5 * 3600 + 30 * 60;
const char *BROKER = "broker.emqx.io";
const char *LOG_FILE = "/log.csv";   // epoch_utc,t,h,flow_l_per_min,litres_in_interval
const char *SENT_FILE = "/sent.txt"; // byte offset of the first row not yet uploaded
const size_t LOG_MAX_BYTES = 120000;

DHTesp dht;
RTC_DS1307 rtc;
Preferences prefs;
WiFiClient mqttNet;
PubSubClient mqtt(mqttNet);
bool rtcOk = false, fsOk = false, pumpOn = false, lastPressed = false;
volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }
uint32_t logPulses = 0, livePulses = 0, startPulses = 0, windowPulses = 0;
unsigned long lastLog = 0, lastCheckin = 0, lastLive = 0, lastMqttTry = 0, windowStart = 0, pressedSince = 0;
unsigned long wifiStartedAt = 0;
int wifiStage = 0;  // 0 idle, 1 trying college WiFi, 2 trying hotspot, 3 connected
String name, collegeSsid, collegePass, message = "Lab Station ready";

// ---------- time ----------
bool timeValid() { return rtcOk && rtc.now().year() >= 2024; }
uint32_t nowUtc() { return timeValid() ? rtc.now().unixtime() - IST_OFFSET_S : 0; }
String timeText() {
  if (!timeValid()) return "clock not set";
  DateTime n = rtc.now();
  char b[20];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
  return b;
}
void setClockUtc(uint32_t utc) { if (rtcOk && utc > 1700000000) rtc.adjust(DateTime(utc + IST_OFFSET_S)); }

// ---------- pump ----------
void setRelay(bool on) { digitalWrite(RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW); }
void pump(bool on, const String &reason) {
  if (on == pumpOn) return;
  pumpOn = on;
  setRelay(on);
  digitalWrite(LED, on ? HIGH : LOW);
  if (on) { windowStart = millis(); startPulses = windowPulses = pulses; message = "Pump started (" + reason + ")"; }
  else message = "Pump stopped (" + reason + "), " + String((pulses - startPulses) / PULSES_PER_LITRE, 2) + " litres this run";
  Serial.println(message);
  lastLive = 0;
}

// ---------- logging ----------
void logReading() {
  TempAndHumidity r = dht.getTempAndHumidity();
  bool ok = dht.getStatus() == DHTesp::ERROR_NONE;
  uint32_t p = pulses, d = p - logPulses;
  logPulses = p;
  float litres = d / PULSES_PER_LITRE;
  float lpm = litres / (LOG_EVERY_MS / 60000.0);
  uint32_t ts = nowUtc();
  Serial.printf("[log] %s  T=%s H=%s  %.2f L\n", timeText().c_str(), ok ? String(r.temperature, 1).c_str() : "-", ok ? String(r.humidity, 1).c_str() : "-", litres);
  if (!fsOk || ts == 0) return;  // no time yet: nothing useful to store
  File f = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%s,%s,%.2f,%.3f\n", (unsigned long)ts, ok ? String(r.temperature, 1).c_str() : "", ok ? String(r.humidity, 1).c_str() : "", lpm, litres);
  f.close();
}

size_t sentOffset() {
  File f = LittleFS.open(SENT_FILE, FILE_READ);
  size_t v = f ? f.readString().toInt() : 0;
  if (f) f.close();
  return v;
}
void setSentOffset(size_t v) { File f = LittleFS.open(SENT_FILE, FILE_WRITE); if (f) { f.print(v); f.close(); } }

// Keep the log small: once it is big, drop what was already uploaded.
void compactLog() {
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (!f || f.size() < LOG_MAX_BYTES) { if (f) f.close(); return; }
  size_t off = sentOffset();
  if (off == 0) off = f.size() / 2;  // nothing uploaded for a long time: keep the newest half
  f.seek(off);
  if (off) f.readStringUntil('\n');
  File out = LittleFS.open("/log.tmp", FILE_WRITE);
  uint8_t buf[256];
  while (f.available()) out.write(buf, f.read(buf, sizeof(buf)));
  f.close(); out.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/log.tmp", LOG_FILE);
  setSentOffset(0);
}

// ---------- WiFi ----------
void loadWifi() {
  prefs.begin("lab", true);
  collegeSsid = prefs.getString("ssid", "");
  collegePass = prefs.getString("pass", "");
  prefs.end();
}
void saveWifi(const String &s, const String &p) {
  prefs.begin("lab", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
  collegeSsid = s; collegePass = p;
  Serial.println("Saved college WiFi: " + s);
}
void startWifi(int stage) {
  WiFi.disconnect(true);
  delay(100);
  wifiStage = stage;
  wifiStartedAt = millis();
  if (stage == 1) { Serial.println("WiFi: trying " + collegeSsid); WiFi.begin(collegeSsid.c_str(), collegePass.c_str()); }
  else { Serial.println("WiFi: trying update hotspot " LAB_HOTSPOT_SSID); WiFi.begin(LAB_HOTSPOT_SSID, LAB_HOTSPOT_PASS); }
}
void manageWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiStage != 3) {
      wifiStage = 3;
      Serial.println("WiFi connected: " + WiFi.SSID() + "  IP " + WiFi.localIP().toString());
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      lastCheckin = 0;  // check in straight away
    }
    return;
  }
  if (wifiStage == 3) { Serial.println("WiFi lost"); wifiStage = 0; wifiStartedAt = millis(); }
  if (wifiStage == 0 && millis() - wifiStartedAt > (wifiStartedAt ? 60000UL : 0UL)) startWifi(collegeSsid.length() ? 1 : 2);
  else if (wifiStage == 1 && millis() - wifiStartedAt > 20000) startWifi(2);
  else if (wifiStage == 2 && millis() - wifiStartedAt > 20000) { WiFi.disconnect(true); wifiStage = 0; wifiStartedAt = millis(); }
}
void wifiSetupPortal() {
  Serial.println("WiFi setup: join " + name + "-setup (password 12345678) from a phone");
  digitalWrite(LED, HIGH);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (wm.startConfigPortal((name + "-setup").c_str(), "12345678")) saveWifi(wm.getWiFiSSID(), wm.getWiFiPass());
  digitalWrite(LED, pumpOn ? HIGH : LOW);
  wifiStage = 0; wifiStartedAt = 0;
}

// ---------- server check-in and OTA ----------
void installUpdate(const String &id) {
  Serial.println("Update " + id + ": downloading...");
  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http;
  http.begin(tls, String(LAB_SERVER) + "/api/lab/update?device=" + name);
  http.addHeader("x-device-token", LAB_TOKEN);
  http.setTimeout(20000);
  int code = http.GET();
  String result;
  if (code == 200) {
    int len = http.getSize();
    if (Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) {
      size_t written = Update.writeStream(*http.getStreamPtr());
      if (Update.end(true)) result = "installed (" + String(written) + " bytes)";
      else result = "failed: " + String(Update.errorString());
    } else result = "failed: " + String(Update.errorString());
  } else result = "failed: HTTP " + String(code);
  http.end();
  Serial.println("Update " + id + ": " + result);

  HTTPClient ack;
  WiFiClientSecure tls2; tls2.setInsecure();
  ack.begin(tls2, String(LAB_SERVER) + "/api/lab/update");
  ack.addHeader("Content-Type", "application/json");
  ack.addHeader("x-device-token", LAB_TOKEN);
  ack.POST("{\"device\":\"" + name + "\",\"id\":\"" + id + "\",\"result\":\"" + result + "\"}");
  ack.end();
  if (result.startsWith("installed")) { Serial.println("Restarting into the new firmware..."); delay(500); ESP.restart(); }
}

void checkin() {
  // Build the upload: up to 150 logged rows that have not been uploaded yet.
  JsonDocument doc;
  doc["device"] = name;
  doc["version"] = LAB_VERSION;
  doc["program"] = "Lab Station";
  doc["ip"] = WiFi.localIP().toString();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = WiFi.RSSI();
  JsonArray rows = doc["rows"].to<JsonArray>();
  size_t start = sentOffset(), end = start;
  if (fsOk) {
    File f = LittleFS.open(LOG_FILE, FILE_READ);
    if (f && start < f.size()) {
      f.seek(start);
      for (int i = 0; i < 150 && f.available(); i++) {
        String line = f.readStringUntil('\n');
        end = f.position();
        int a = line.indexOf(','), b = line.indexOf(',', a + 1), c = line.indexOf(',', b + 1), d = line.indexOf(',', c + 1);
        if (a < 0 || d < 0) continue;
        JsonObject r = rows.add<JsonObject>();
        r["ts"] = line.substring(0, a).toInt();
        String t = line.substring(a + 1, b), h = line.substring(b + 1, c);
        if (t.length()) r["t"] = t.toFloat(); else r["t"] = nullptr;
        if (h.length()) r["h"] = h.toFloat(); else r["h"] = nullptr;
        r["f"] = line.substring(c + 1, d).toFloat();
        r["l"] = line.substring(d + 1).toFloat();
      }
    }
    if (f) f.close();
  }
  String body;
  serializeJson(doc, body);

  WiFiClientSecure tls; tls.setInsecure();
  HTTPClient http;
  http.begin(tls, String(LAB_SERVER) + "/api/lab/checkin");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-token", LAB_TOKEN);
  http.setTimeout(15000);
  int code = http.POST(body);
  String reply = code == 200 ? http.getString() : "";
  http.end();
  if (code != 200) { Serial.printf("[check-in] failed: HTTP %d\n", code); return; }

  if (end > start) setSentOffset(end);
  JsonDocument res;
  if (deserializeJson(res, reply)) return;
  Serial.printf("[check-in] ok, uploaded %d rows\n", (int)res["saved"]);
  uint32_t serverNow = res["now"] | 0;
  if (serverNow && (!timeValid() || abs((long)nowUtc() - (long)serverNow) > 120)) { setClockUtc(serverNow); Serial.println("Clock set from the server"); }
  if (!res["update"].isNull()) installUpdate(res["update"]["id"].as<String>());
  compactLog();
}

// ---------- MQTT (same as program 9, so the students' apps work) ----------
void onMessage(char *topic, byte *payload, unsigned int len) {
  String cmd;
  for (unsigned int i = 0; i < len; i++) cmd += (char)payload[i];
  cmd.trim(); cmd.toUpperCase();
  if (cmd == "PUMP ON" || cmd == "LED ON") pump(true, "app");
  if (cmd == "PUMP OFF" || cmd == "LED OFF") pump(false, "app");
}
void publishLive() {
  uint32_t p = pulses;
  float lpm = (p - livePulses) / (LIVE_EVERY_MS / 1000.0) / 7.5;
  livePulses = p;
  TempAndHumidity r = dht.getTempAndHumidity();
  bool ok = dht.getStatus() == DHTesp::ERROR_NONE;
  String json = "{\"name\":\"" + name + "\",\"time\":\"" + timeText() + "\",\"t\":" + (ok ? String(r.temperature, 1) : String("null")) +
                ",\"h\":" + (ok ? String(r.humidity, 1) : String("null")) + ",\"f\":" + String(lpm, 2) + ",\"l\":" +
                String(p / PULSES_PER_LITRE, 2) + ",\"pump\":" + (pumpOn ? "true" : "false") + ",\"msg\":\"" + message + "\"}";
  mqtt.publish(("agrisense/workshop/" + name + "/data").c_str(), json.c_str(), true);
}

// ---------- serial commands ----------
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  String up = line; up.toUpperCase();
  int y, mo, d, h, mi, s;
  if (up.startsWith("WIFI ") && line.indexOf(',') > 5) {
    saveWifi(line.substring(5, line.indexOf(',')), line.substring(line.indexOf(',') + 1));
    WiFi.disconnect(true);
    wifiStage = 0;
    wifiStartedAt = 0;  // reconnect straight away with the new WiFi
  } else if (sscanf(line.c_str(), "T %d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6 && rtcOk) { rtc.adjust(DateTime(y, mo, d, h, mi, s)); Serial.println("Clock set"); }
  else if (up == "PUMP ON") pump(true, "serial");
  else if (up == "PUMP OFF") pump(false, "serial");
  else if (up == "STATUS" || up.length()) {
    Serial.printf("Lab Station %s  %s  time %s  WiFi %s  college WiFi '%s'  log %s\n", LAB_VERSION, name.c_str(), timeText().c_str(),
                  WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "offline", collegeSsid.c_str(), fsOk ? "ok" : "no flash");
    Serial.println("Commands: WIFI name,password  |  T 2026-10-01 10:30:00  |  PUMP ON / PUMP OFF  |  STATUS");
  }
}

void setup() {
  Serial.begin(115200);
  setRelay(false);
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(FLOW_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);
  dht.setup(DHT_PIN, DHTesp::DHT22);
  Wire.begin(21, 22);
  rtcOk = rtc.begin();
  fsOk = LittleFS.begin(true);
  loadWifi();

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);

  Serial.printf("\nLab Station %s  device %s\n", LAB_VERSION, name.c_str());
  Serial.println(collegeSsid.length() ? "College WiFi: " + collegeSsid : String("No college WiFi saved: hold BOOT 5 s, or type WIFI name,password"));
  Serial.println("Type STATUS for help.");
}

void loop() {
  manageWifi();
  handleSerial();

  // BOOT: short press switches the pump, hold 5 s opens WiFi setup
  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) pressedSince = millis();
  if (!pressed && lastPressed && millis() - pressedSince < 3000) pump(!pumpOn, "button");
  if (pressed && pressedSince && millis() - pressedSince > 5000) { pressedSince = 0; wifiSetupPortal(); }
  lastPressed = pressed;

  // dry-run protection
  if (pumpOn && millis() - windowStart >= NO_FLOW_STOP_MS) {
    if (pulses - windowPulses < MIN_PULSES) { pump(false, "NO FLOW"); message = "NO FLOW - pump stopped to protect the motor"; }
    else { windowStart = millis(); windowPulses = pulses; }
  }

  // internet time -> RTC (once per connection)
  static bool ntpDone = false;
  time_t now = time(nullptr);
  if (!ntpDone && now > 1700000000) { setClockUtc(now); ntpDone = true; Serial.println("Clock set from the internet"); }

  if (lastLog == 0 || millis() - lastLog >= LOG_EVERY_MS) { lastLog = millis(); logReading(); }

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected() && (lastMqttTry == 0 || millis() - lastMqttTry > 10000)) {
      lastMqttTry = millis();
      String topicStatus = "agrisense/workshop/" + name + "/status";
      if (mqtt.connect((name + "-" + String((uint32_t)esp_random(), HEX)).c_str(), topicStatus.c_str(), 1, true, "offline")) {
        mqtt.publish(topicStatus.c_str(), "online", true);
        mqtt.subscribe(("agrisense/workshop/" + name + "/cmd").c_str());
      }
    }
    mqtt.loop();
    if (mqtt.connected() && (lastLive == 0 || millis() - lastLive >= LIVE_EVERY_MS)) { lastLive = millis(); publishLive(); }
    if (lastCheckin == 0 || millis() - lastCheckin >= CHECKIN_EVERY_MS) { lastCheckin = millis(); checkin(); }
  }
}
