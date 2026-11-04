#include "net.h"

#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <deque>

#include "app.h"
#include "commands.h"
#include "settings.h"
#include "web.h"

namespace net {

static void scanStore(int n);
static void scanPoll();

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
static bool scanning = false;
static String scanCache = "[]";
static bool scanWanted = false;     // a scan was refused (radio busy): try again
static uint32_t scanWantedSince = 0, lastScanTry = 0;
static uint32_t apIdleSince = 0;    // last time a phone was on the SoftAP
static String subCmd, subZone, subAll;

// PubSubClient/WiFiClient are not thread-safe. App and web-page commands run
// in the web server's task, so only the main loop task touches the MQTT
// client: other tasks queue publishes and ask for reconfigure/forget, and
// read a flag for "MQTT up". (A publish from the web task during a broker
// reconnect crashed the node in WiFiClient::available, 0.1.5.)
static TaskHandle_t loopTask = nullptr;
static SemaphoreHandle_t qLock = nullptr;
struct Outgoing {
  String sub, payload;
  bool retain;
};
static std::deque<Outgoing> outQ;
static const size_t OUTQ_MAX = 40;
static volatile bool mqttIsUp = false;
static volatile bool reconfigWanted = false;
static volatile bool forgetWanted = false;
static bool onLoopTask() { return loopTask == nullptr || xTaskGetCurrentTaskHandle() == loopTask; }
static void doReconfigure();
static void doForget();

static const uint32_t STA_FAIL_TO_AP_MS = 10UL * 60 * 1000;  // farm WiFi missing 10 min -> SoftAP
static const uint32_t AP_WINDOW_MS = 15UL * 60 * 1000;
static const uint32_t STA_RETRY_MS = 30000;
// While a phone is on the SoftAP, the node does not try the farm WiFi: every
// attempt makes the radio hop channels, so the phone drops off the SoftAP and
// WiFi scans are refused (tracker WM2-004, same bug as FG1-003). Retries start
// again this long after the last phone leaves.
static const uint32_t AP_QUIET_MS = 30000;

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
bool mqttUp() { return mqttIsUp; }
bool apOn() { return apActive; }
String ip() { return wifiUp() ? WiFi.localIP().toString() : String(""); }
int rssi() { return wifiUp() ? WiFi.RSSI() : 0; }
int apClients() { return apActive ? WiFi.softAPgetStationNum() : 0; }

void startAp(uint32_t minutes) {
  String ssid = cfg.apSsid();
  if (!apActive) {
    WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);  // modem sleep makes the SoftAP drop phones
    WiFi.softAP(ssid.c_str(), cfg.apPassword().c_str());
    apActive = true;
    apIdleSince = millis();
    Serial.printf("[net] SoftAP %s up, password %s, http://192.168.4.1\n", ssid.c_str(), cfg.apPassword().c_str());
  }
  apUntil = minutes ? millis() + minutes * 60000UL : 0;
}

static void stopAp() {
  if (!apActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
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
  if (!onLoopTask()) {
    if (!mqttIsUp) return false;
    xSemaphoreTake(qLock, portMAX_DELAY);
    bool ok = outQ.size() < OUTQ_MAX;
    if (ok) outQ.push_back({String(sub), payload, retain});
    xSemaphoreGive(qLock);
    return ok;
  }
  if (!mqtt.connected()) return false;
  String t = cfg.base() + sub;
  return mqtt.publish(t.c_str(), (const uint8_t*)payload.c_str(), payload.length(), retain);
}

static void drainQueue() {
  for (int n = 0; n < 10; n++) {
    xSemaphoreTake(qLock, portMAX_DELAY);
    if (outQ.empty()) {
      xSemaphoreGive(qLock);
      return;
    }
    Outgoing o = outQ.front();
    outQ.pop_front();
    xSemaphoreGive(qLock);
    if (!mqtt.connected()) return;  // dropped: the store-and-forward log covers records
    String t = cfg.base() + o.sub;
    mqtt.publish(t.c_str(), (const uint8_t*)o.payload.c_str(), o.payload.length(), o.retain);
  }
}

void reconfigure() {
  if (!onLoopTask()) {
    reconfigWanted = true;
    return;
  }
  doReconfigure();
}

static void doReconfigure() {
  if (mqtt.connected()) mqtt.disconnect();
  mqttIsUp = false;
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
  loopTask = xTaskGetCurrentTaskHandle();
  qLock = xSemaphoreCreateMutex();
  WiFi.persistent(false);
  // Retries are done here in loop(), not by the core's auto-reconnect: that
  // one re-joins every few seconds and keeps the radio busy.
  WiFi.setAutoReconnect(false);
  WiFi.setHostname(cfg.hostname().c_str());
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);
  // One scan at boot, before joining or starting the SoftAP, so the app's
  // first network list is instant.
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false);
  if (n >= 0) scanStore(n);
  Serial.printf("[net] %d WiFi networks found\n", n);
  if (cfg.ssid.length()) {
    WiFi.setSleep(true);  // modem-sleep between beacons (Responsive profile)
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    lastStaTry = millis();
    Serial.printf("[net] joining %s\n", cfg.ssid.c_str());
  }
  if (!cfg.ssid.length()) {
    Serial.println("[net] no farm WiFi set: SoftAP stays on until configured");
    startAp(0);
  } else {
    // SoftAP for 15 min after every power-on (tracker WM2-015), so a phone can
    // always reach the node on site; it stays on while a phone is connected
    // and goes off afterwards once the farm WiFi is up.
    Serial.printf("[net] SoftAP for 15 min after %s\n", doubleReset ? "double reset" : "power on");
    startAp(15);
  }
  staLostSince = millis();
}

void loop() {
  uint32_t now = millis();
  if (forgetWanted) {
    forgetWanted = false;
    doForget();
  }
  if (reconfigWanted) {
    reconfigWanted = false;
    doReconfigure();
  }
  scanPoll();
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

  // Retry the farm WiFi every 30 s, but not while a phone is on the SoftAP.
  bool phone = apActive && WiFi.softAPgetStationNum() > 0;
  if (phone) {
    if (!up && apIdleSince != 0 && cfg.ssid.length()) {
      WiFi.disconnect();  // stop a join in progress
      Serial.println("[net] phone on SoftAP: farm WiFi retries paused");
    }
    apIdleSince = 0;
  } else if (apActive && apIdleSince == 0) {
    apIdleSince = now;
  }
  bool quiet = apActive && (phone || (apIdleSince && now - apIdleSince < AP_QUIET_MS));
  if (!up && cfg.ssid.length() && !quiet && !scanning && now - lastStaTry > STA_RETRY_MS) {
    lastStaTry = now;
    WiFi.disconnect();
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }
  if (scanWanted && !scanning && now - lastScanTry >= 1000) {
    if (now - scanWantedSince > 15000) {
      scanWanted = false;  // give up: send the cached list so the app stops waiting
      web::broadcast("{\"type\":\"wifi_scan\",\"data\":" + scanCache + "}");
    } else {
      scanRequest();
    }
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
    drainQueue();
  }
  mqttIsUp = up && mqtt.connected();
}

static uint32_t scanAt = 0;

static void scanStore(int n) {
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n && a.size() < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    bool dup = false;
    for (JsonObject o : a)
      if (ssid == (const char*)o["ssid"]) dup = true;
    if (dup) continue;
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  scanCache = "";
  serializeJson(d, scanCache);
  scanAt = millis();
  WiFi.scanDelete();
}

void scanRequest() {
  if (scanning) return;
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);  // scanning needs the station interface
  // A join in progress makes the driver refuse the scan: stop it first (the
  // retry in loop() picks it up again later).
  if (!wifiUp() && WiFi.status() != WL_IDLE_STATUS) WiFi.disconnect();
  lastScanTry = millis();
  if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
    if (!scanWanted) scanWantedSince = millis();
    scanWanted = true;  // try again shortly
    Serial.println("[net] scan refused, retrying");
    return;
  }
  scanWanted = false;
  scanning = true;
}

bool scanBusy() { return scanning; }
uint32_t scanAgeS() { return scanAt ? (millis() - scanAt) / 1000 + 1 : 0; }

void scanCached(JsonArray out) {
  JsonDocument d;
  if (deserializeJson(d, scanCache)) return;
  for (JsonVariant v : d.as<JsonArray>()) out.add(v);
}

static void scanPoll() {
  if (!scanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanning = false;
  if (n >= 0) scanStore(n);
  web::broadcast("{\"type\":\"wifi_scan\",\"data\":" + scanCache + "}");
}

void forgetWifi() {
  cfg.ssid = "";
  cfg.pass = "";
  cfg.save();
  if (!onLoopTask()) {
    forgetWanted = true;
    return;
  }
  doForget();
}

static void doForget() {
  if (mqtt.connected()) mqtt.disconnect();
  mqttIsUp = false;
  WiFi.disconnect(true);
  if (mdnsUp) { MDNS.end(); mdnsUp = false; }
  apActive = false;
  startAp(0);
  Serial.println("[net] farm WiFi forgotten; SoftAP on");
}

}  // namespace net
