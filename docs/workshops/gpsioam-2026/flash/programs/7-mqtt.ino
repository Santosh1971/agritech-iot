// 7 · Internet (MQTT): send temperature & humidity to the internet, see them from anywhere.
// First start: join WiFi "FarmIoT-xxxx-setup" (password 12345678) from a phone, pick the
// room's WiFi and enter its password. The ESP32 remembers it.
// Then open https://agrisenseandcontrol.in/workshop/mqtt and type the device name.
// Topics: agrisense/workshop/FarmIoT-xxxx/data (readings), .../cmd (send LED ON / LED OFF)
// Wiring: DHT22 data -> GPIO 4 (VCC -> 3V3, GND -> GND).

#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHTesp.h>

const int DHT_PIN = 4;
const int LED = 2;
const char *BROKER = "broker.emqx.io";  // free public broker: anyone can read these topics

DHTesp dht;
WiFiClient net;
PubSubClient mqtt(net);
String name, topicData, topicStatus, topicCmd;
unsigned long lastRead = 0, lastTry = 0;

void onMessage(char *topic, byte *payload, unsigned int len) {
  String cmd;
  for (unsigned int i = 0; i < len; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "LED ON") digitalWrite(LED, HIGH);
  if (cmd == "LED OFF") digitalWrite(LED, LOW);
  Serial.println("Command from the internet: " + cmd);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  dht.setup(DHT_PIN, DHTesp::DHT22);

  uint64_t mac = ESP.getEfuseMac();
  char id[5];
  snprintf(id, sizeof(id), "%04X", (unsigned)((mac >> 32) & 0xFFFF));
  name = String("FarmIoT-") + id;
  topicData = "agrisense/workshop/" + name + "/data";
  topicStatus = "agrisense/workshop/" + name + "/status";
  topicCmd = "agrisense/workshop/" + name + "/cmd";

  Serial.println("Program 7: device name " + name);
  Serial.println("If WiFi is not set up: join " + name + "-setup (password 12345678) from a phone");
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect((name + "-setup").c_str(), "12345678")) ESP.restart();
  Serial.println("WiFi connected");

  mqtt.setServer(BROKER, 1883);
  mqtt.setCallback(onMessage);
}

void loop() {
  if (!mqtt.connected() && (lastTry == 0 || millis() - lastTry > 5000)) {
    lastTry = millis();
    if (mqtt.connect((name + "-" + String((uint32_t)esp_random(), HEX)).c_str(), topicStatus.c_str(), 1, true, "offline")) {
      mqtt.publish(topicStatus.c_str(), "online", true);
      mqtt.subscribe(topicCmd.c_str());
      Serial.println("Connected to the internet broker. Readings go to " + topicData);
    } else {
      Serial.println("Broker not reachable, retrying...");
    }
  }
  mqtt.loop();

  if (millis() - lastRead >= 5000) {
    lastRead = millis();
    TempAndHumidity r = dht.getTempAndHumidity();
    String json = dht.getStatus() != DHTesp::ERROR_NONE
        ? String("{\"name\":\"") + name + "\",\"error\":\"sensor not found\"}"
        : String("{\"name\":\"") + name + "\",\"t\":" + String(r.temperature, 1) + ",\"h\":" + String(r.humidity, 1) + "}";
    Serial.println(json);
    if (mqtt.connected()) mqtt.publish(topicData.c_str(), json.c_str(), true);
  }
}
