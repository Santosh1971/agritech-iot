// Lab 4 - Reference solution: phone dashboard served by the ESP32 itself
// Students should generate their own version with an AI assistant using the Lab 4 prompt;
// this file is the facilitators' fallback and answer key.
//
// The ESP32 starts its own WiFi "FarmNode-01" (password farm12345). Join it from a phone
// and open http://192.168.4.1  -> live temp, humidity, soil %, and Pump ON/OFF/AUTO.
// Real hardware only: Wokwi cannot let a phone join a simulated access point.
//
// Wiring: DHT22 DATA -> GPIO 4, soil sensor AOUT -> GPIO 34, relay IN -> GPIO 26 (active HIGH)
// Library: "DHT sensor library for ESPx". WebServer is built into the ESP32 core.

#include <WiFi.h>
#include <WebServer.h>
#include "DHTesp.h"

const char* AP_SSID = "FarmNode-01";
const char* AP_PASS = "farm12345";      // at least 8 characters

const int DHT_PIN = 4, SOIL_PIN = 34, PUMP_PIN = 26;
const int DRY = 3000, WET = 1300;
const int ON_BELOW = 35, OFF_ABOVE = 60;

DHTesp dht;
WebServer server(80);

float tempC = NAN, humidity = NAN;
int soilPct = 0;
bool pumpOn = false;
bool autoMode = false;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FarmNode-01</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f3f6f1;color:#17231c}
h1{font-size:22px;margin:0 0 12px}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
.card{background:#fff;border:1px solid #cbd6cc;border-radius:12px;padding:12px;text-align:center}
.v{font-size:28px;font-weight:700}.l{font-size:13px;color:#4a5a50}
.pump{margin-top:16px;padding:16px;border-radius:12px;background:#fff;border:1px solid #cbd6cc}
button{font-size:18px;padding:14px;border-radius:10px;border:0;width:32%;color:#fff}
.on{background:#0e5e43}.off{background:#9b2c1f}.auto{background:#2466a8}
#state{font-size:20px;font-weight:700;margin-bottom:10px}
</style></head><body>
<h1>FarmNode-01</h1>
<div class="grid">
 <div class="card"><div class="v" id="t">--</div><div class="l">Temp &deg;C</div></div>
 <div class="card"><div class="v" id="h">--</div><div class="l">Humidity %</div></div>
 <div class="card"><div class="v" id="s">--</div><div class="l">Soil %</div></div>
</div>
<div class="pump"><div id="state">Pump: --</div>
 <button class="on" onclick="cmd('on')">ON</button>
 <button class="off" onclick="cmd('off')">OFF</button>
 <button class="auto" onclick="cmd('auto')">AUTO</button>
</div>
<script>
function show(d){
 document.getElementById('t').textContent=d.temp;
 document.getElementById('h').textContent=d.hum;
 document.getElementById('s').textContent=d.soil;
 document.getElementById('state').textContent='Pump: '+(d.pump?'ON':'OFF')+(d.auto?' (auto)':' (manual)');
}
function refresh(){fetch('/data').then(r=>r.json()).then(show).catch(()=>{});}
function cmd(c){fetch('/pump?set='+c).then(r=>r.json()).then(show);}
setInterval(refresh,2000);refresh();
</script></body></html>
)HTML";

void applyPump() { digitalWrite(PUMP_PIN, pumpOn ? HIGH : LOW); }

String json() {
  return String("{\"temp\":\"") + (isnan(tempC) ? "--" : String(tempC, 1)) +
         "\",\"hum\":\"" + (isnan(humidity) ? "--" : String(humidity, 0)) +
         "\",\"soil\":" + soilPct +
         ",\"pump\":" + (pumpOn ? "true" : "false") +
         ",\"auto\":" + (autoMode ? "true" : "false") + "}";
}

void handleRoot() { server.send_P(200, "text/html", PAGE); }
void handleData() { server.send(200, "application/json", json()); }
void handlePump() {
  String set = server.arg("set");
  if (set == "on")   { autoMode = false; pumpOn = true; }
  if (set == "off")  { autoMode = false; pumpOn = false; }
  if (set == "auto") { autoMode = true; }
  applyPump();
  server.send(200, "application/json", json());
}

void setup() {
  Serial.begin(115200);
  pinMode(PUMP_PIN, OUTPUT);
  applyPump();
  dht.setup(DHT_PIN, DHTesp::DHT22);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("Join WiFi " + String(AP_SSID) + " and open http://" + WiFi.softAPIP().toString());
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/pump", handlePump);
  server.begin();
}

uint32_t lastRead = 0;
void loop() {
  server.handleClient();
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    TempAndHumidity d = dht.getTempAndHumidity();
    if (dht.getStatus() == 0) { tempC = d.temperature; humidity = d.humidity; }
    soilPct = constrain(map(analogRead(SOIL_PIN), DRY, WET, 0, 100), 0, 100);
    if (autoMode) {
      if (soilPct < ON_BELOW)  pumpOn = true;
      if (soilPct > OFF_ABOVE) pumpOn = false;
      applyPump();
    }
  }
}
