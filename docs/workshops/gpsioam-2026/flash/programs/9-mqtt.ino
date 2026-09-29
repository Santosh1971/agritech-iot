// 9 · Internet (MQTT): see the farm and switch the pump from anywhere.
// First start: join WiFi "FarmIoT-xxxx-setup" (password 12345678) from a phone, pick the room's
// WiFi and enter its password. The ESP32 remembers it. The clock is then set from the internet.
// Open https://agrisenseandcontrol.in/workshop/mqtt and type the device name (FarmIoT-xxxx).
// Topics: agrisense/workshop/FarmIoT-xxxx/data (every 5 s), /status (online/offline),
//         /cmd (send PUMP ON / PUMP OFF)
// BOOT switches the pump too; the blue LED shows the pump state (the "pump" on a bare DevKit).
// Wiring (ESP32 DevKit V1): DHT22 -> D23, flow -> D35 (via divider), relay -> D19, RTC D21/D22.

#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHTesp.h>
#include <time.h>

const int DHT_PIN = 23, FLOW_PIN = 35, RELAY = 19, BUTTON = 0, LED = 2;
const bool RELAY_ACTIVE_LOW = false;  // this kit's relay turns ON with HIGH; change to true for an active-LOW module
const unsigned long NO_FLOW_STOP_MS = 10000;
const uint32_t MIN_PULSES = 5;
const float PULSES_PER_LITRE = 450.0;
const char *BROKER = "broker.emqx.io";  // free public broker: anyone can read these topics
const long IST_OFFSET_S = 5 * 3600 + 30 * 60;

WiFiClient net;
PubSubClient mqtt(net);
DHTesp dht;
RTC_DS1307 rtc;
bool rtcOk = false, clockSynced = false, pumpOn = false, lastPressed = false;
volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }
uint32_t lastPulses = 0, startPulses = 0, windowPulses = 0;
unsigned long lastSend = 0, lastTry = 0, windowStart = 0, lastRateAt = 0;
String name, topicData, topicStatus, topicCmd, message = "Ready";

String timeNow() {
  if (!rtcOk) return "no clock";
  DateTime n = rtc.now();
  if (n.year() < 2024) return "clock not set";
  char b[20];
  snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d", n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
  return b;
}

void setRelay(bool on) { digitalWrite(RELAY, (on != RELAY_ACTIVE_LOW) ? HIGH : LOW); }

void pump(bool on, const String &reason) {
  if (on == pumpOn) return;
  pumpOn = on;
  setRelay(on);
  digitalWrite(LED, on ? HIGH : LOW);
  if (on) { windowStart = millis(); startPulses = windowPulses = pulses; message = "Pump started (" + reason + ")"; }
  else message = "Pump stopped (" + reason + "), " + String((pulses - startPulses) / PULSES_PER_LITRE, 2) + " litres this run";
  Serial.println(message);
  lastSend = 0;  // publish the change straight away
}

void onMessage(char *topic, byte *payload, unsigned int len) {
  String cmd;
  for (unsigned int i = 0; i < len; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "PUMP ON" || cmd == "LED ON") pump(true, "internet");
  if (cmd == "PUMP OFF" || cmd == "LED OFF") pump(false, "internet");
}

String num(float v, int d) { return isnan(v) ? String("null") : String(v, d); }

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

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;
  topicData = "agrisense/workshop/" + name + "/data";
  topicStatus = "agrisense/workshop/" + name + "/status";
  topicCmd = "agrisense/workshop/" + name + "/cmd";

  Serial.println("Program 9: device name " + name);
  Serial.println("If WiFi is not set up: join " + name + "-setup (password 12345678) from a phone");
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect((name + "-setup").c_str(), "12345678")) ESP.restart();
  Serial.println("WiFi connected");
  configTime(IST_OFFSET_S, 0, "pool.ntp.org", "time.google.com");

  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
}

void loop() {
  // Copy internet time into the RTC once it arrives
  time_t now = time(nullptr);
  if (!clockSynced && rtcOk && now > 1700000000) {
    struct tm *t = localtime(&now);
    rtc.adjust(DateTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec));
    clockSynced = true;
    Serial.println("Clock set from the internet");
  }

  if (!mqtt.connected() && (lastTry == 0 || millis() - lastTry > 5000)) {
    lastTry = millis();
    if (mqtt.connect((name + "-" + String((uint32_t)esp_random(), HEX)).c_str(), topicStatus.c_str(), 1, true, "offline")) {
      mqtt.publish(topicStatus.c_str(), "online", true);
      mqtt.subscribe(topicCmd.c_str());
      Serial.println("Connected to the internet broker. Open agrisenseandcontrol.in/workshop/mqtt and type " + name);
    } else {
      Serial.println("Broker not reachable, retrying...");
    }
  }
  mqtt.loop();

  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) { pump(!pumpOn, "button"); delay(30); }
  lastPressed = pressed;

  if (pumpOn && millis() - windowStart >= NO_FLOW_STOP_MS) {
    if (pulses - windowPulses < MIN_PULSES) { pump(false, "NO FLOW"); message = "NO FLOW - pump stopped to protect the motor"; }
    else { windowStart = millis(); windowPulses = pulses; }
  }

  if (lastSend == 0 || millis() - lastSend >= 5000) {
    lastSend = millis();
    unsigned long elapsed = lastRateAt == 0 ? 1000 : max(1000UL, millis() - lastRateAt);
    lastRateAt = millis();
    uint32_t total = pulses;
    float litresPerMin = (total - lastPulses) / (elapsed / 1000.0) / 7.5;
    lastPulses = total;
    TempAndHumidity r = dht.getTempAndHumidity();
    bool ok = dht.getStatus() == DHTesp::ERROR_NONE;
    String json = "{\"name\":\"" + name + "\",\"time\":\"" + timeNow() + "\",\"t\":" + num(ok ? r.temperature : NAN, 1) +
                  ",\"h\":" + num(ok ? r.humidity : NAN, 1) + ",\"f\":" + String(litresPerMin, 2) + ",\"l\":" +
                  String(total / PULSES_PER_LITRE, 2) + ",\"pump\":" + (pumpOn ? "true" : "false") +
                  ",\"msg\":\"" + message + "\"}";
    Serial.println(json);
    if (mqtt.connected()) mqtt.publish(topicData.c_str(), json.c_str(), true);
  }
}
