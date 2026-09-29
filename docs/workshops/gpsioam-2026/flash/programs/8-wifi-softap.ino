// 8 · WiFi (own network): the ESP32 makes its own WiFi and serves a farm dashboard to your phone.
// Phone: join WiFi "FarmIoT-xxxx" (password 12345678) and open http://192.168.4.1
// The page shows time, temperature, humidity, flow and litres, has big PUMP ON / OFF buttons
// (with the no-flow check), and a button to set the clock from your phone.
// BOOT switches the pump too; the blue LED shows the pump state (the "pump" on a bare DevKit).
// Wiring (ESP32 DevKit V1): DHT22 -> D23, flow -> D35 (via divider), relay -> D19, RTC D21/D22.

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHTesp.h>

const int DHT_PIN = 23, FLOW_PIN = 35, RELAY = 19, BUTTON = 0, LED = 2;
const bool RELAY_ACTIVE_LOW = false;  // this kit's relay turns ON with HIGH; change to true for an active-LOW module
const unsigned long NO_FLOW_STOP_MS = 10000;
const uint32_t MIN_PULSES = 5;
const float PULSES_PER_LITRE = 450.0;

WebServer server(80);
DHTesp dht;
RTC_DS1307 rtc;
bool rtcOk = false, pumpOn = false, lastPressed = false;
volatile uint32_t pulses = 0;
void IRAM_ATTR onPulse() { pulses++; }
uint32_t lastPulses = 0, startPulses = 0, windowPulses = 0;
unsigned long lastRead = 0, windowStart = 0;
float temperature = NAN, humidity = NAN, litresPerMin = 0;
String name, message = "Ready";

const char PAGE[] = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Farm IoT</title><style>body{font-family:sans-serif;background:#f3f6f1;margin:0;padding:16px;color:#17231c;text-align:center}
.g{display:grid;grid-template-columns:1fr 1fr;gap:10px;max-width:420px;margin:0 auto}.c{background:#fff;border-radius:12px;padding:12px}
.v{font-size:32px;font-weight:bold;color:#0e5e43}.m{margin:12px auto;max-width:420px;padding:10px;border-radius:10px;background:#e8eee5}
button{font-size:20px;padding:14px 22px;margin:6px;border-radius:12px;border:0;color:#fff;background:#0e5e43}.off{background:#9b2c1f}
small{color:#4a5a50}</style></head><body><h2 id="n">Farm IoT</h2><div id="tm"><small>--</small></div>
<div class="g"><div class="c">Temperature<div class="v"><span id="t">--</span>&deg;C</div></div>
<div class="c">Humidity<div class="v"><span id="h">--</span>%</div></div>
<div class="c">Flow<div class="v"><span id="f">--</span></div><small>L/min</small></div>
<div class="c">Total<div class="v"><span id="l">--</span></div><small>litres</small></div></div>
<div class="m"><b>Pump: <span id="p">--</span></b><br><span id="msg"></span></div>
<button onclick="fetch('/pump?on=1')">PUMP ON</button><button class="off" onclick="fetch('/pump?on=0')">PUMP OFF</button>
<p><small><a href="#" onclick="setClock();return false">Set the device clock from this phone</a></small></p>
<script>async function r(){try{const d=await (await fetch('/data')).json();for(const k of['t','h','f','l'])document.getElementById(k).textContent=d[k]??'--';
document.getElementById('n').textContent=d.name;document.getElementById('tm').innerHTML='<small>'+d.time+'</small>';
document.getElementById('p').textContent=d.pump?'ON':'OFF';document.getElementById('msg').textContent=d.msg;}catch(e){}}
function setClock(){const n=new Date(),p=x=>String(x).padStart(2,'0');fetch('/time?t='+n.getFullYear()+'-'+p(n.getMonth()+1)+'-'+p(n.getDate())+'T'+p(n.getHours())+':'+p(n.getMinutes())+':'+p(n.getSeconds()));}
r();setInterval(r,2000);</script></body></html>)HTML";

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
  WiFi.softAP(name.c_str(), "12345678");

  server.on("/", [] { server.send(200, "text/html", PAGE); });
  server.on("/data", [] {
    String json = "{\"name\":\"" + name + "\",\"time\":\"" + timeNow() + "\",\"t\":" + num(temperature, 1) +
                  ",\"h\":" + num(humidity, 1) + ",\"f\":" + String(litresPerMin, 2) + ",\"l\":" +
                  String(pulses / PULSES_PER_LITRE, 2) + ",\"pump\":" + (pumpOn ? "true" : "false") +
                  ",\"msg\":\"" + message + "\"}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  server.on("/pump", [] { pump(server.arg("on") == "1", "phone"); server.send(200, "text/plain", pumpOn ? "ON" : "OFF"); });
  server.on("/time", [] {
    int y, mo, d, h, mi, s;
    if (rtcOk && sscanf(server.arg("t").c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6) {
      rtc.adjust(DateTime(y, mo, d, h, mi, s));
      message = "Clock set from phone";
    }
    server.send(200, "text/plain", timeNow());
  });
  server.begin();
  Serial.println("Program 8: join WiFi " + name + " (password 12345678) and open http://192.168.4.1");
}

void loop() {
  server.handleClient();

  bool pressed = digitalRead(BUTTON) == LOW;
  if (pressed && !lastPressed) { pump(!pumpOn, "button"); delay(30); }
  lastPressed = pressed;

  if (pumpOn && millis() - windowStart >= NO_FLOW_STOP_MS) {
    if (pulses - windowPulses < MIN_PULSES) { pump(false, "NO FLOW"); message = "NO FLOW - pump stopped to protect the motor"; }
    else { windowStart = millis(); windowPulses = pulses; }
  }

  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    uint32_t total = pulses;
    litresPerMin = (total - lastPulses) / 2.0 / 7.5;
    lastPulses = total;
    TempAndHumidity r = dht.getTempAndHumidity();
    bool ok = dht.getStatus() == DHTesp::ERROR_NONE;
    temperature = ok ? r.temperature : NAN;
    humidity = ok ? r.humidity : NAN;
  }
}
