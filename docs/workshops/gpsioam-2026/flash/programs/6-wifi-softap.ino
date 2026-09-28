// 6 · WiFi SoftAP: the ESP32 makes its own WiFi network and shows readings on a phone.
// Phone: join WiFi "FarmIoT-xxxx" (password 12345678), then open http://192.168.4.1
// The page updates every 3 seconds and has buttons to switch the blue LED.
// Wiring: DHT22 data -> GPIO 4 (VCC -> 3V3, GND -> GND).

#include <WiFi.h>
#include <WebServer.h>
#include <DHTesp.h>

const int DHT_PIN = 4;
const int LED = 2;

DHTesp dht;
WebServer server(80);
float temperature = NAN, humidity = NAN;
unsigned long lastRead = 0;

const char PAGE[] = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Farm IoT</title><style>body{font-family:sans-serif;text-align:center;background:#f3f6f1;margin:0;padding:24px}
.v{font-size:56px;font-weight:bold;color:#0e5e43}.c{background:#fff;border-radius:12px;padding:14px;margin:12px auto;max-width:320px}
button{font-size:18px;padding:12px 20px;margin:6px;border-radius:10px;border:0;background:#0e5e43;color:#fff}</style></head>
<body><h2 id="n">Farm IoT</h2>
<div class="c">Temperature<div class="v"><span id="t">--</span> &deg;C</div></div>
<div class="c">Humidity<div class="v"><span id="h">--</span> %</div></div>
<button onclick="fetch('/led?on=1')">LED ON</button><button onclick="fetch('/led?on=0')">LED OFF</button>
<script>async function r(){try{const d=await (await fetch('/data')).json();
document.getElementById('n').textContent=d.name;document.getElementById('t').textContent=d.t??'--';
document.getElementById('h').textContent=d.h??'--';}catch(e){}}r();setInterval(r,3000);</script></body></html>)HTML";

String name;

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;

  WiFi.softAP(name.c_str(), "12345678");
  server.on("/", [] { server.send(200, "text/html", PAGE); });
  server.on("/data", [] {
    String json = "{\"name\":\"" + name + "\",\"t\":" + (isnan(temperature) ? String("null") : String(temperature, 1)) +
                  ",\"h\":" + (isnan(humidity) ? String("null") : String(humidity, 1)) + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  server.on("/led", [] {
    bool on = server.arg("on") == "1";
    digitalWrite(LED, on ? HIGH : LOW);
    server.send(200, "text/plain", on ? "LED ON" : "LED OFF");
  });
  server.begin();
  Serial.println("Program 6: join WiFi " + name + " (password 12345678) and open http://192.168.4.1");
}

void loop() {
  server.handleClient();
  if (millis() - lastRead >= 3000) {
    lastRead = millis();
    TempAndHumidity r = dht.getTempAndHumidity();
    bool ok = dht.getStatus() == DHTesp::ERROR_NONE;
    temperature = ok ? r.temperature : NAN;
    humidity = ok ? r.humidity : NAN;
    Serial.println(ok ? "Temperature " + String(temperature, 1) + " C, Humidity " + String(humidity, 1) + " %"
                      : String("Sensor not found - check the DHT22 on GPIO 4"));
  }
}
