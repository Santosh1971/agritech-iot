// 7 · Bluetooth: everything on your phone. Every 2 seconds the ESP32 sends the time, temperature,
// humidity, flow, litres and pump state over classic Bluetooth.
// Phone: pair with "FarmIoT-xxxx", open Serial Bluetooth Terminal and connect.
// Type in the app: PUMP ON · PUMP OFF · PROTECT OFF (stop the no-flow check) · PROTECT ON
// BOOT button also switches the pump. The blue LED shows the pump state, so on a bare DevKit
// the LED is your "pump".
// Wiring (ESP32 DevKit V1): DHT22 -> D23, flow sensor -> D35 (via divider), relay -> D19,
// RTC SDA -> D21, SCL -> D22. Any part not connected is simply reported as missing.

#include <BluetoothSerial.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHTesp.h>

const int DHT_PIN = 23, FLOW_PIN = 35, RELAY = 19, BUTTON = 0, LED = 2;
const bool RELAY_ACTIVE_LOW = false;  // this kit's relay turns ON with HIGH; change to true for an active-LOW module
const unsigned long NO_FLOW_STOP_MS = 10000;
const uint32_t MIN_PULSES = 5;
const float PULSES_PER_LITRE = 450.0;

BluetoothSerial bt;
DHTesp dht;
RTC_DS1307 rtc;  // works with DS3231 and DS1307
bool rtcOk = false, pumpOn = false, protect = true, lastPressed = false;
volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }
uint32_t lastPulses = 0, startPulses = 0, windowPulses = 0;
unsigned long lastSend = 0, windowStart = 0;
float litresPerMin = 0;
String name;

void say(const String &line) { Serial.println(line); bt.println(line); }

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
  if (on) { windowStart = millis(); startPulses = windowPulses = pulses; }
  float litres = (pulses - startPulses) / PULSES_PER_LITRE;
  say(on ? "PUMP ON (" + reason + ")" : "PUMP OFF (" + reason + "), " + String(litres, 2) + " litres this run");
}

void handle(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "PUMP ON" || cmd == "LED ON" || cmd == "ON") pump(true, "phone");
  else if (cmd == "PUMP OFF" || cmd == "LED OFF" || cmd == "OFF") pump(false, "phone");
  else if (cmd == "PROTECT OFF") { protect = false; say("No-flow check OFF"); }
  else if (cmd == "PROTECT ON") { protect = true; say("No-flow check ON"); }
  else if (cmd.length()) say("Commands: PUMP ON, PUMP OFF, PROTECT ON, PROTECT OFF");
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

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;
  bt.begin(name);
  Serial.println("Program 7: Bluetooth name " + name + " - pair your phone with it");
}

void loop() {
  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) { pump(!pumpOn, "button"); delay(30); }
  lastPressed = pressed;

  if (bt.available()) handle(bt.readStringUntil('\n'));
  if (Serial.available()) handle(Serial.readStringUntil('\n'));

  if (pumpOn && protect && millis() - windowStart >= NO_FLOW_STOP_MS) {
    if (pulses - windowPulses < MIN_PULSES) { say("NO FLOW - pump stopped to protect the motor"); pump(false, "no flow"); }
    else { windowStart = millis(); windowPulses = pulses; }
  }

  if (millis() - lastSend >= 2000) {
    lastSend = millis();
    uint32_t total = pulses;
    litresPerMin = (total - lastPulses) / 2.0 / 7.5;
    lastPulses = total;
    TempAndHumidity r = dht.getTempAndHumidity();
    String th = dht.getStatus() == DHTesp::ERROR_NONE
        ? String(r.temperature, 1) + " C, " + String(r.humidity, 1) + " %"
        : String("no sensor");
    say(timeNow() + " | " + th + " | flow " + String(litresPerMin, 2) + " L/min, total " +
        String(total / PULSES_PER_LITRE, 2) + " L | pump " + (pumpOn ? "ON" : "OFF"));
  }
}
