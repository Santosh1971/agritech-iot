// TH Monitor - Offline logger version
// Saves a reading every LOG_INTERVAL_S seconds to a CSV file in flash (it survives power loss).
// The board also makes its own WiFi, so a phone can download the log when it comes near -
// no router or internet needed.
//
//   WiFi name: TH-xxxx (last 4 hex digits of the MAC)   Password: 12345678
//
//   GET  /                          dashboard: live values, rows stored, download, set clock
//   GET  /api/now                   {"id":"TH-addb","ts":"2026-10-01T10:30:00","t":28.4,"h":61.2}
//   GET  /api/info                  {"id":..,"rows":1234,"bytes":32084,"freeBytes":..,"intervalS":60}
//   GET  /api/history?from=TS       stored rows newer than TS, oldest first, max 500 per call:
//                                   {"rows":[{"ts":..,"t":..,"h":..},...],"more":false}
//                                   (no "from" = from the beginning; call again with the last ts
//                                   while "more" is true)
//   GET  /log.csv                   the whole log as a CSV file
//   POST /api/time                  set the clock; body: 2026-10-01T10:30:00
//   POST /api/clear                 delete the log
//
// Serial monitor (115200): "T 2026-10-01 10:30:00" sets the clock, "D" dumps the log.
//
// Wiring: DHT22 data D7, DS3231 SDA D3 / SCL D4, blue LED D2 (blinks when a row is saved).

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>

const uint8_t DHT_PIN = D7;
const uint8_t SDA_PIN = D3;
const uint8_t SCL_PIN = D4;
const uint8_t LED_PIN = D2;

const char *AP_PASSWORD = "12345678";
const unsigned long LOG_INTERVAL_S = 60;  // one row per minute = ~37 KB per day
const size_t MAX_LOG_BYTES = 1000000;     // ~27 days; then the oldest half is dropped
const char *LOG_FILE = "/log.csv";        // each line: 2026-10-01T10:30:00,28.4,61.2
const int ROWS_PER_CALL = 500;

DHT dht(DHT_PIN, DHT22);
RTC_DS3231 rtc;
ESP8266WebServer server(80);

String deviceId;
bool rtcOk = false;
long rowCount = 0;
float lastT = NAN, lastH = NAN;
char lastTs[20] = "";
unsigned long lastLog = 0;

// ---------- clock ----------

void timestamp(char *out) {
  if (!rtcOk) {
    strcpy(out, "unknown");
    return;
  }
  DateTime now = rtc.now();
  snprintf(out, 20, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
}

bool setClock(const String &text) {  // "2026-10-01T10:30:00" or "2026-10-01 10:30:00"
  int y, mo, d, h, mi, s;
  if (sscanf(text.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return false;
  rtc.adjust(DateTime(y, mo, d, h, mi, s));
  return true;
}

// ---------- log file ----------

long countRows() {
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) return 0;
  long n = 0;
  while (f.available()) {
    if (f.read() == '\n') n++;
  }
  f.close();
  return n;
}

// When the log is full, keep only its newer half.
void trimLog() {
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f || f.size() < MAX_LOG_BYTES) {
    if (f) f.close();
    return;
  }
  f.seek(f.size() / 2);
  f.readStringUntil('\n');  // skip the partial line
  File out = LittleFS.open("/log.tmp", "w");
  uint8_t buf[256];
  while (f.available()) out.write(buf, f.read(buf, sizeof(buf)));
  f.close();
  out.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/log.tmp", LOG_FILE);
  rowCount = countRows();
  Serial.printf("Log was full - kept the newest %ld rows\n", rowCount);
}

void readAndLog() {
  lastT = dht.readTemperature();
  lastH = dht.readHumidity();
  timestamp(lastTs);
  if (isnan(lastT) || isnan(lastH)) {
    Serial.printf("%s DHT22 read failed - nothing saved\n", lastTs);
    return;
  }
  digitalWrite(LED_PIN, HIGH);
  File f = LittleFS.open(LOG_FILE, "a");
  f.printf("%s,%.1f,%.1f\n", lastTs, lastT, lastH);
  f.close();
  rowCount++;
  digitalWrite(LED_PIN, LOW);
  Serial.printf("{\"ts\":\"%s\",\"t\":%.1f,\"h\":%.1f}  saved, %ld rows\n", lastTs, lastT, lastH,
                rowCount);
  trimLog();
}

// ---------- web server ----------

const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TH Logger</title>
<style>
 body{font-family:sans-serif;text-align:center;margin:0;padding:24px;background:#f4f7f4;color:#223}
 .v{font-size:48px;font-weight:bold;margin:4px 0} .u{font-size:20px;color:#667}
 .card{background:#fff;border-radius:12px;padding:14px;margin:10px auto;max-width:320px;box-shadow:0 1px 4px #0002}
 small{color:#667} button,a.b{display:inline-block;padding:10px 14px;font-size:15px;margin:6px 4px;
 border:1px solid #889;border-radius:6px;background:#eee;color:#223;text-decoration:none}
</style></head><body>
<h2 id="id">TH Logger</h2>
<div class="card">Temperature<div class="v"><span id="t">--</span><span class="u"> &deg;C</span></div></div>
<div class="card">Humidity<div class="v"><span id="h">--</span><span class="u"> %</span></div></div>
<small id="ts">waiting...</small>
<div class="card"><b id="rows">--</b> readings stored<br><small id="space"></small><br>
<a class="b" href="/log.csv" download="th-log.csv">Download CSV</a></div>
<button onclick="setTime()">Set device clock from this phone</button><br>
<button onclick="clearLog()">Delete all readings</button>
<script>
async function refresh(){
  try{
    const r=await (await fetch('/api/now')).json();
    const i=await (await fetch('/api/info')).json();
    document.getElementById('id').textContent=i.id;
    document.getElementById('t').textContent=r.t??'--';
    document.getElementById('h').textContent=r.h??'--';
    document.getElementById('ts').textContent=r.error?r.error:'Measured '+r.ts;
    document.getElementById('rows').textContent=i.rows;
    document.getElementById('space').textContent=
      Math.round(i.bytes/1024)+' KB used, one reading every '+i.intervalS+' s';
  }catch(e){document.getElementById('ts').textContent='Not connected';}
}
async function setTime(){
  const d=new Date(), p=n=>String(n).padStart(2,'0');
  const ts=d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+'T'+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds());
  alert((await (await fetch('/api/time',{method:'POST',body:ts})).json()).message);
}
async function clearLog(){
  if(!confirm('Delete all stored readings?'))return;
  alert((await (await fetch('/api/clear',{method:'POST'})).json()).message); refresh();
}
refresh(); setInterval(refresh,5000);
</script></body></html>)HTML";

void sendJson(int code, const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");  // lets web/mobile apps call the API
  server.send(code, "application/json", body);
}

String valueJson(const char *ts, float t, float h) {
  return String("{\"ts\":\"") + ts + "\",\"t\":" + String(t, 1) + ",\"h\":" + String(h, 1) + "}";
}

void handleNow() {
  if (!lastTs[0]) {
    sendJson(503, "{\"error\":\"no reading yet\"}");
  } else if (isnan(lastT) || isnan(lastH)) {
    sendJson(200, "{\"id\":\"" + deviceId + "\",\"ts\":\"" + lastTs + "\",\"error\":\"DHT22 read failed\"}");
  } else {
    sendJson(200, "{\"id\":\"" + deviceId + "\"," + valueJson(lastTs, lastT, lastH).substring(1));
  }
}

void handleInfo() {
  FSInfo fs;
  LittleFS.info(fs);
  File f = LittleFS.open(LOG_FILE, "r");
  size_t bytes = f ? f.size() : 0;
  if (f) f.close();
  sendJson(200, "{\"id\":\"" + deviceId + "\",\"rows\":" + rowCount + ",\"bytes\":" + bytes +
                    ",\"freeBytes\":" + (fs.totalBytes - fs.usedBytes) +
                    ",\"intervalS\":" + LOG_INTERVAL_S + "}");
}

// Rows newer than ?from=, streamed in pieces so a big log doesn't run out of memory.
void handleHistory() {
  String from = server.arg("from");  // ISO timestamps sort correctly as plain text
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "{\"rows\":[");

  File f = LittleFS.open(LOG_FILE, "r");
  int sent = 0;
  bool more = false;
  String chunk;
  while (f && f.available()) {
    String line = f.readStringUntil('\n');  // 2026-10-01T10:30:00,28.4,61.2
    int c1 = line.indexOf(','), c2 = line.lastIndexOf(',');
    if (c1 < 0 || c2 <= c1) continue;
    String ts = line.substring(0, c1);
    if (from.length() && ts <= from) continue;
    if (sent == ROWS_PER_CALL) {
      more = true;
      break;
    }
    if (sent++) chunk += ",";
    chunk += "{\"ts\":\"" + ts + "\",\"t\":" + line.substring(c1 + 1, c2) +
             ",\"h\":" + line.substring(c2 + 1) + "}";
    if (chunk.length() > 1000) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  if (f) f.close();
  server.sendContent(chunk + "],\"more\":" + (more ? "true" : "false") + "}");
  server.sendContent("");  // end of response
}

void handleCsv() {
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    server.send(404, "text/plain", "no readings yet");
    return;
  }
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + deviceId + "-log.csv\"");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleTime() {
  if (setClock(server.arg("plain"))) {
    sendJson(200, "{\"ok\":true,\"message\":\"Clock set\"}");
  } else {
    sendJson(400, "{\"ok\":false,\"message\":\"Send the time as 2026-10-01T10:30:00\"}");
  }
}

void handleClear() {
  LittleFS.remove(LOG_FILE);
  rowCount = 0;
  sendJson(200, "{\"ok\":true,\"message\":\"All readings deleted\"}");
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
  Serial.println("\nTH Monitor - Offline logger");

  pinMode(LED_PIN, OUTPUT);
  dht.begin();

  Wire.begin(SDA_PIN, SCL_PIN);
  rtcOk = rtc.begin();
  if (!rtcOk) {
    Serial.println("ERROR: DS3231 RTC not found - check SDA/SCL wiring and power");
  } else if (rtc.lostPower() || rtc.now().year() < 2024) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("WARNING: RTC time was lost - set it from the phone, or timestamps will be wrong");
  }

  if (!LittleFS.begin()) {  // first start: the file system is formatted automatically
    Serial.println("ERROR: could not mount the file system");
  }
  rowCount = countRows();
  Serial.printf("Log has %ld rows\n", rowCount);

  String mac = WiFi.softAPmacAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  deviceId = "TH-" + mac.substring(8);

#ifdef ALSO_JOIN_SAVED_WIFI
  // Instructor/test option: also join the last saved router WiFi, so a laptop on the same
  // network can call the API. Build with: PLATFORMIO_BUILD_FLAGS=-DALSO_JOIN_SAVED_WIFI
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin();
#else
  WiFi.mode(WIFI_AP);
#endif
  WiFi.softAP(deviceId.c_str(), AP_PASSWORD);
  Serial.printf("WiFi: %s  password: %s  open http://%s\n", deviceId.c_str(), AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", PAGE); });
  server.on("/api/now", HTTP_GET, handleNow);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/log.csv", HTTP_GET, handleCsv);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("T ") && setClock(line.substring(2))) {
      Serial.println("RTC time set");
    } else if (line == "D") {  // dump the log
      File f = LittleFS.open(LOG_FILE, "r");
      while (f && f.available()) Serial.write(f.read());
      if (f) f.close();
      Serial.println("-- end of log --");
    }
  }

  if (lastLog == 0 || millis() - lastLog >= LOG_INTERVAL_S * 1000) {
    lastLog = millis();
    readAndLog();
  }
}
