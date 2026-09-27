// TH Monitor - MQTT version
// Joins your WiFi, sets the clock from the internet, and publishes a reading every
// interval to a public MQTT broker. Your app subscribes to the same broker.
//
// First start: the board opens WiFi "TH-xxxx" (password 12345678). Join it from a phone,
// a setup page opens (or go to http://192.168.4.1) - pick your WiFi and enter its password.
// The board remembers it. To forget it, send "W" on the serial monitor.
//
// Topics (xxxx = last 4 hex digits of the MAC):
//   agrisense/th/TH-xxxx/data    {"id":"TH-xxxx","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}  (retained)
//   agrisense/th/TH-xxxx/status  online | offline                                          (retained)
//   agrisense/th/TH-xxxx/cmd     send: read | interval 30 | time 2026-10-01T10:30:00
//
// Wiring: DHT22 data D7, DS3231 SDA D3 / SCL D4, blue LED D2 (on = connected to MQTT).

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <time.h>

const uint8_t DHT_PIN = D7;
const uint8_t SDA_PIN = D3;
const uint8_t SCL_PIN = D4;
const uint8_t LED_PIN = D2;

const char *MQTT_BROKER = "broker.emqx.io";  // free public broker - anyone can read your data
const int MQTT_PORT = 1883;
const char *TOPIC_ROOT = "agrisense/th/";
const char *SETUP_PASSWORD = "12345678";
const long TIMEZONE_OFFSET_S = 5 * 3600 + 30 * 60;  // IST = UTC+5:30

unsigned long readIntervalS = 30;  // can be changed with the "interval" command

DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String deviceId, topicData, topicStatus, topicCmd;
bool rtcOk = false;
unsigned long lastRead = 0, lastMqttTry = 0;

// ---------- clock ----------

bool setClock(const String &text) {  // "2026-10-01T10:30:00" or "2026-10-01 10:30:00"
  int y, mo, d, h, mi, s;
  if (sscanf(text.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
  rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

// Internet time (NTP) arrives in the background a few seconds after WiFi connects.
// loop() calls this; once the time is valid it is copied into the RTC once.
bool clockSynced = false;
void syncClockFromInternet() {
  time_t now = time(nullptr);
  if (clockSynced || !rtcOk || now < 1700000000) return;  // not arrived yet
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

// ---------- reading ----------

void readAndPublish() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  char ts[20];
  timestamp(ts);

  String json = "{\"id\":\"" + deviceId + "\",\"ts\":\"" + ts + "\"";
  if (isnan(t) || isnan(h)) {
    json += ",\"error\":\"DHT22 read failed\"}";
  } else {
    json += ",\"t\":" + String(t, 1) + ",\"h\":" + String(h, 1) + "}";
  }

  Serial.println(json);
  if (mqtt.connected()) {
    mqtt.publish(topicData.c_str(), json.c_str(), true);  // retained: new subscribers get it at once
    digitalWrite(LED_PIN, LOW);                            // short blink = sent
    delay(50);
    digitalWrite(LED_PIN, HIGH);
  }
}

// ---------- MQTT ----------

void onMessage(char *topic, byte *payload, unsigned int length) {
  String cmd;
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  cmd.trim();
  Serial.println("Command: " + cmd);

  if (cmd == "read") {
    readAndPublish();
  } else if (cmd.startsWith("interval ")) {
    long s = cmd.substring(9).toInt();
    if (s >= 5) readIntervalS = s;  // DHT22 can't read faster than every 2 s; 5 s minimum
    Serial.printf("Interval is now %lu s\n", readIntervalS);
  } else if (cmd.startsWith("time ")) {
    Serial.println(setClock(cmd.substring(5)) ? "RTC time set" : "Bad time format");
  }
}

void connectMqtt() {
  Serial.printf("Connecting to MQTT %s ... ", MQTT_BROKER);
  String clientId = deviceId + "-" + String(ESP.getChipId(), HEX);
  // Last will: if the board drops off, the broker publishes "offline" for us.
  if (mqtt.connect(clientId.c_str(), topicStatus.c_str(), 1, true, "offline")) {
    Serial.println("connected");
    mqtt.publish(topicStatus.c_str(), "online", true);
    mqtt.subscribe(topicCmd.c_str());
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.printf("failed (state %d), retrying in 5 s\n", mqtt.state());
    digitalWrite(LED_PIN, LOW);
  }
}

// ---------- setup & loop ----------

void setup() {
  Serial.begin(115200);
  delay(1000);  // DHT22 needs ~1 s after power-up
  Serial.println("\nTH Monitor - MQTT");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();
  Wire.begin(SDA_PIN, SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) Serial.println("ERROR: DS3231 RTC not found - check SDA/SCL wiring and power");

  String mac = WiFi.macAddress();  // "C4:5B:BE:6C:AD:DB"
  mac.replace(":", "");
  mac.toLowerCase();
  deviceId = "TH-" + mac.substring(8);
  topicData = String(TOPIC_ROOT) + deviceId + "/data";
  topicStatus = String(TOPIC_ROOT) + deviceId + "/status";
  topicCmd = String(TOPIC_ROOT) + deviceId + "/cmd";

  // Connect to the saved WiFi, or open the setup page for 3 minutes if there is none.
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  Serial.printf("WiFi: connecting (if not set up, join %s / %s from a phone)\n",
                deviceId.c_str(), SETUP_PASSWORD);
  if (!wm.autoConnect(deviceId.c_str(), SETUP_PASSWORD)) {
    Serial.println("No WiFi - restarting");
    ESP.restart();
  }
  Serial.println("WiFi connected, IP " + WiFi.localIP().toString());

  configTime(TIMEZONE_OFFSET_S, 0, "pool.ntp.org", "time.google.com");  // start getting internet time

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMessage);
  Serial.println("Publishing to " + topicData);
  Serial.println("Send commands to " + topicCmd);
}

void loop() {
  syncClockFromInternet();

  if (!mqtt.connected() && (lastMqttTry == 0 || millis() - lastMqttTry > 5000)) {
    lastMqttTry = millis();
    connectMqtt();
  }
  mqtt.loop();

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "W") {  // forget WiFi and restart into the setup page
      WiFiManager().resetSettings();
      ESP.restart();
    } else if (line.startsWith("T ") && setClock(line.substring(2))) {
      Serial.println("RTC time set");
    }
  }

  if (lastRead == 0 || millis() - lastRead >= readIntervalS * 1000) {
    lastRead = millis();
    readAndPublish();
  }
}
