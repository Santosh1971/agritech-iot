// TH Monitor - SoftAP version
// The board creates its own WiFi network. Join it from a phone or laptop and open
// http://192.168.4.1 for a live dashboard, or call the JSON API from your app.
//
//   WiFi name: TH-xxxx (last 4 hex digits of the board's MAC)   Password: 12345678
//
//   GET  /              live dashboard page
//   GET  /api/now       latest reading   {"id":"TH-0f6d","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}
//   GET  /api/history   last readings    [{"ts":...,"t":...,"h":...}, ...]  (oldest first)
//   POST /api/time      set the clock; body: 2026-10-01T10:30:00
//
// Serial monitor (115200) prints every reading; "T 2026-10-01 10:30:00" sets the clock.
//
// Wiring: DHT22 data D7, DS3231 SDA D3 / SCL D4, blue LED D2.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>

const uint8_t DHT_PIN = D7;
const uint8_t SDA_PIN = D3;
const uint8_t SCL_PIN = D4;
const uint8_t LED_PIN = D2;

const char *AP_PASSWORD = "12345678";      // at least 8 characters
const unsigned long READ_INTERVAL_S = 10;  // how often to read, in seconds
const int HISTORY_SIZE = 60;               // readings kept in memory (60 x 10 s = 10 min)

DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
ESP8266WebServer server(80);

struct Reading {
  char ts[20];
  float t, h;  // NAN if the sensor read failed
};
Reading history[HISTORY_SIZE];
int historyCount = 0;  // how many slots are filled
int historyNext = 0;   // where the next reading goes (ring buffer)

String deviceId;
bool rtcOk = false;
unsigned long lastRead = 0;

// ---------- sensors ----------

void timestamp(char *out) {
  if (!rtcOk) {
    strcpy(out, "unknown");
    return;
  }
  DateTime now = rtc.now();
  snprintf(out, 20, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

String readingJson(const Reading &r) {
  String json = String("{\"ts\":\"") + r.ts + "\"";
  if (isnan(r.t) || isnan(r.h)) {
    json += ",\"error\":\"DHT22 read failed\"}";
  } else {
    json += ",\"t\":" + String(r.t, 1) + ",\"h\":" + String(r.h, 1) + "}";
  }
  return json;
}

void takeReading() {
  digitalWrite(LED_PIN, HIGH);  // LED blinks on every reading
  Reading &r = history[historyNext];
  r.t = dht.readTemperature();
  r.h = dht.readHumidity();
  timestamp(r.ts);
  historyNext = (historyNext + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
  Serial.println(readingJson(r));
  digitalWrite(LED_PIN, LOW);
}

bool setClock(const String &text) {  // accepts "2026-10-01T10:30:00" or "2026-10-01 10:30:00"
  int y, mo, d, h, mi, s;
  if (sscanf(text.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
  rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

// ---------- web server ----------

const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TH Monitor</title>
<style>
 body{font-family:sans-serif;text-align:center;margin:0;padding:24px;background:#f4f7f4;color:#223}
 .v{font-size:56px;font-weight:bold;margin:4px 0} .u{font-size:24px;color:#667}
 .card{background:#fff;border-radius:12px;padding:16px;margin:12px auto;max-width:320px;box-shadow:0 1px 4px #0002}
 small{color:#667} button{padding:10px 16px;font-size:15px;margin-top:8px}
</style></head><body>
<h2 id="id">TH Monitor</h2>
<div class="card">Temperature<div class="v"><span id="t">--</span><span class="u"> &deg;C</span></div></div>
<div class="card">Humidity<div class="v"><span id="h">--</span><span class="u"> %</span></div></div>
<small id="ts">waiting...</small><br>
<button onclick="setTime()">Set device clock from this phone</button>
<script>
async function refresh(){
  try{
    const r=await (await fetch('/api/now')).json();
    document.getElementById('id').textContent=r.id;
    document.getElementById('t').textContent=r.t??'--';
    document.getElementById('h').textContent=r.h??'--';
    document.getElementById('ts').textContent=r.error?r.error:'Measured '+r.ts;
  }catch(e){document.getElementById('ts').textContent='Not connected';}
}
async function setTime(){
  const d=new Date(), p=n=>String(n).padStart(2,'0');
  const ts=d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+'T'+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());
  const r=await fetch('/api/time',{method:'POST',body:ts});
  alert(await r.text());
}
refresh(); setInterval(refresh,5000);
</script></body></html>)HTML";

void sendJson(int code, const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");  // lets web/mobile apps call the API
  server.send(code, "application/json", body);
}

void handleNow() {
  if (historyCount == 0) {
    sendJson(503, "{\"error\":\"no reading yet\"}");
    return;
  }
  const Reading &r = history[(historyNext - 1 + HISTORY_SIZE) % HISTORY_SIZE];
  String json = readingJson(r);
  json = "{\"id\":\"" + deviceId + "\"," + json.substring(1);  // add the device id
  sendJson(200, json);
}

void handleHistory() {
  String json = "[";
  int start = (historyNext - historyCount + HISTORY_SIZE) % HISTORY_SIZE;  // oldest
  for (int i = 0; i < historyCount; i++) {
    if (i) json += ",";
    json += readingJson(history[(start + i) % HISTORY_SIZE]);
  }
  sendJson(200, json + "]");
}

void handleTime() {
  if (setClock(server.arg("plain"))) {
    sendJson(200, "{\"ok\":true,\"message\":\"Clock set\"}");
  } else {
    sendJson(400, "{\"ok\":false,\"message\":\"Send the time as 2026-10-01T10:30:00\"}");
  }
}

void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {  // CORS preflight from browser-based apps
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
    return;
  }
  sendJson(404, "{\"error\":\"not found\"}");
}

// ---------- setup & loop ----------

void setup() {
  Serial.begin(115200);
  delay(1000);  // DHT22 needs ~1 s after power-up
  Serial.println("\nTH Monitor - SoftAP");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  Wire.begin(SDA_PIN, SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("ERROR: DS3231 RTC not found - check SDA/SCL wiring and power");
  } else if (rtc.lostPower() || rtc.now().year() < 2024) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC time was not set - set it from the app or with the T command");
  }

  // WiFi name from the last 4 hex digits of the MAC, e.g. TH-0f6d
  String mac = WiFi.softAPmacAddress();  // "98:CD:AC:31:0F:6D"
  mac.replace(":", "");
  mac.toLowerCase();
  deviceId = "TH-" + mac.substring(8);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(deviceId.c_str(), AP_PASSWORD);
  Serial.printf("WiFi: %s  password: %s  open http://%s\n", deviceId.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/now", HTTP_GET, handleNow);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/time", HTTP_POST, handleTime);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();

  if (Serial.available()) {  // "T 2026-10-01 10:30:00" sets the clock
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("T ") && setClock(line.substring(2))) Serial.println("RTC time set");
    else if (line.length()) Serial.println("To set the clock: T 2026-10-01 10:30:00");
  }

  if (lastRead == 0 || millis() - lastRead >= READ_INTERVAL_S * 1000) {
    lastRead = millis();
    takeReading();
  }
}
