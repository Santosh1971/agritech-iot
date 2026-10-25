#include "net.h"

#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "app.h"
#include "commands.h"
#include "settings.h"

namespace net {

static WiFiClient tcp;
static PubSubClient mqtt(tcp);
static bool apActive = false;
static uint32_t apUntil = 0;        // millis; 0 = no timeout
static uint32_t staLostSince = 0;
static uint32_t lastStaTry = 0;
static uint32_t lastMqttTry = 0;
static uint32_t lastNtpSync = 0;
static bool mdnsUp = false;
static bool ntpStarted = false;
static bool wasUp = false;
static String subCmd, subZone, subAll;

static const uint32_t STA_FAIL_TO_AP_MS = 10UL * 60 * 1000;  // farm WiFi missing 10 min -> SoftAP
static const uint32_t AP_WINDOW_MS = 15UL * 60 * 1000;

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
bool mqttUp() { return mqtt.connected(); }
bool apOn() { return apActive; }
String ip() { return wifiUp() ? WiFi.localIP().toString() : String(""); }
int rssi() { return wifiUp() ? WiFi.RSSI() : 0; }
int apClients() { return apActive ? WiFi.softAPgetStationNum() : 0; }

void startAp(uint32_t minutes) {
  String ssid = "WM2-" + cfg.zone + "-" + cfg.node;
  if (!apActive) {
    WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(ssid.c_str(), cfg.apPassword().c_str());
    apActive = true;
    Serial.printf("[net] SoftAP %s up, password %s, http://192.168.4.1\n", ssid.c_str(), cfg.apPassword().c_str());
  }
  apUntil = minutes ? millis() + minutes * 60000UL : 0;
}

static void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println("[net] SoftAP off");
}

static void onMqtt(char* topic, byte* payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned i = 0; i < len; i++) msg += (char)payload[i];
  String resp = dispatchCommand(msg, "mqtt");
  publish("resp", resp);
}

static void mqttConnect() {
  if (cfg.brokerHost().length() == 0 || !wifiUp()) return;
  if (millis() - lastMqttTry < 10000) return;
  lastMqttTry = millis();
  String base = cfg.base();
  subCmd = base + "cmd";
  // Zone/farm-wide commands exist on Girish's broker only; on AgriSense the
  // per-device ACL allows just this device's own topic tree.
  subZone = cfg.broker == 1 ? cfg.farm + "/wm2/" + cfg.zone + "/all/cmd" : "";
  subAll = cfg.broker == 1 ? cfg.farm + "/wm2/all/cmd" : "";
  String clientId = cfg.broker == 0 ? cfg.deviceId() : "wm2-" + cfg.farm + "-" + cfg.zone + "-" + cfg.node;
  String will = base + "status";
  mqtt.setServer(cfg.brokerHost().c_str(), cfg.brokerPort());
  bool ok = mqtt.connect(clientId.c_str(), cfg.brokerUser().length() ? cfg.brokerUser().c_str() : nullptr,
                         cfg.brokerPass().length() ? cfg.brokerPass().c_str() : nullptr, will.c_str(), 1, true, "offline",
                         false /* keep session: commands queued while away */);
  if (!ok) {
    Serial.printf("[mqtt] %s broker %s:%u connect failed (state %d%s)\n", cfg.brokerName(), cfg.brokerHost().c_str(),
                  cfg.brokerPort(), mqtt.state(), mqtt.state() == 4 || mqtt.state() == 5 ? ": check user/password" : "");
    return;
  }
  Serial.printf("[mqtt] connected to %s broker %s as %s, topics %s...\n", cfg.brokerName(), cfg.brokerHost().c_str(),
                clientId.c_str(), base.c_str());
  mqtt.subscribe(subCmd.c_str(), 1);
  if (subZone.length()) mqtt.subscribe(subZone.c_str(), 1);
  if (subAll.length()) mqtt.subscribe(subAll.c_str(), 1);
  mqtt.publish(will.c_str(), "online", true);
  stateChanged();
}

bool publish(const char* sub, const String& payload, bool retain) {
  if (!mqtt.connected()) return false;
  String t = cfg.base() + sub;
  return mqtt.publish(t.c_str(), (const uint8_t*)payload.c_str(), payload.length(), retain);
}

void reconfigure() {
  if (mqtt.connected()) mqtt.disconnect();
  lastMqttTry = 0;
  if (cfg.ssid.length()) {
    WiFi.disconnect();
    WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(cfg.hostname().c_str());
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    lastStaTry = millis();
  }
  if (mdnsUp) { MDNS.end(); mdnsUp = false; }
}

void begin(bool doubleReset) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(cfg.hostname().c_str());
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);
  if (cfg.ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);  // modem-sleep between beacons (Responsive profile)
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    lastStaTry = millis();
    Serial.printf("[net] joining %s\n", cfg.ssid.c_str());
  }
  if (!cfg.ssid.length()) {
    Serial.println("[net] no farm WiFi set: SoftAP stays on until configured");
    startAp(0);
  } else if (doubleReset) {
    Serial.println("[net] double reset: SoftAP for 15 min");
    startAp(15);
  }
  staLostSince = millis();
}

void loop() {
  uint32_t now = millis();
  bool up = wifiUp();

  if (up && !wasUp) {
    Serial.printf("[net] WiFi up, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (!mdnsUp && MDNS.begin(cfg.hostname().c_str())) {
      MDNS.addService("http", "tcp", 80);
      MDNS.addServiceTxt("http", "tcp", "wm2", cfg.zone + "/" + cfg.node);
      mdnsUp = true;
    }
    if (!ntpStarted) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpStarted = true;
    }
  }
  if (!up && wasUp) {
    Serial.println("[net] WiFi lost");
    staLostSince = now;
  }
  wasUp = up;

  // Retry the farm WiFi every 30 s.
  if (!up && cfg.ssid.length() && now - lastStaTry > 30000) {
    lastStaTry = now;
    WiFi.disconnect();
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }

  // SoftAP rules: on after 10 min without farm WiFi; off when its window ends
  // and nobody is connected, if the farm WiFi is configured.
  if (!up && cfg.ssid.length() && !apActive && now - staLostSince > STA_FAIL_TO_AP_MS) startAp(15);
  if (apActive && cfg.ssid.length() && apUntil && (int32_t)(now - apUntil) > 0) {
    if (WiFi.softAPgetStationNum() == 0) {
      if (up) stopAp();
      else { staLostSince = now; stopAp(); }  // off for a while, back after another 10 min
    } else {
      apUntil = now + 60000;  // someone is connected: keep it on
    }
  }

  // NTP -> clock, every 6 h once it succeeds.
  if (up && ntpStarted && (lastNtpSync == 0 || now - lastNtpSync > 6UL * 3600 * 1000)) {
    time_t t = time(nullptr);
    if (t > 1735689600) {  // after 2025-01-01: NTP answered
      setTimeUtc((uint32_t)t, "ntp");
      lastNtpSync = now;
    }
  }

  if (up) {
    if (!mqtt.connected()) mqttConnect();
    mqtt.loop();
  }
}

static bool scanning = false;

void scanStart() {
  if (scanning) return;
  WiFi.scanNetworks(true);
  scanning = true;
}

bool scanResult(JsonArray out) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return false;
  scanning = false;
  for (int i = 0; i < n && i < 20; i++) {
    JsonObject o = out.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  return true;
}

}  // namespace net
