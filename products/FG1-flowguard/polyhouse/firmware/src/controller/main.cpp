// FG1 Polyhouse — Controller (pump panel unit: relay + T/RH sensor).
//
//  * Misting logic (lib/MistLogic) drives the relay, which switches the
//    pump starter coil. Works fully offline; the clock is the DS1307 RTC.
//  * Sensor nodes report over ESP-NOW (Nodes.cpp). The controller's own
//    sensor is the shaded "Outside" reference.
//  * App access: SoftAP "FG1P_xxxx" (192.168.4.1) when no farm WiFi, the
//    farm LAN when connected, and MQTT (Cloud.cpp) when there is internet.
//  * WiFi / fallback handling follows FG1's main.cpp (bench-proven there).
//
// LEDs: on-board = link (as FG1: slow blink SoftAP idle, fast = phone on
//       SoftAP, double blink = cloud OK); green = flashes per node reading;
//       yellow (on the relay driver) = pump ON.
// BOOT held 5 s: forget the farm WiFi and restart into SoftAP.
// Serial: "status", "events", "pump on", "pump off", "auto", "manual",
//         bench checks "cmd <app JSON>", "settime <unix>", "gpio N [0|1]", "i2c" (20x bus scan), "i2cswap", "rtc" (raw DS1307 regs), "sensor" (re-detect).
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <nvs_flash.h>
#include <esp_mac.h>
#include <time.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Config.h"
#include "RTCManager.h"
#include "RelayControl.h"
#include "LEDManager.h"
#include "WiFiScanner.h"
#include "THSensor.h"
#include "EspNowLink.h"
#include "MistLogic.h"
#include "Nodes.h"
#include "WebApi.h"
#include "Cloud.h"

using namespace mist;

static RTCManager   rtc;
static RelayControl relay;
static LEDManager   leds;
static WiFiScanner  scanner;
static THSensor     sensor;
static NodeManager  nodes;
static Controller   mistCtl;
static WebApi       web;
static Cloud        cloud;
static Preferences  prefs;

static String   deviceId;
static uint32_t settingsRev = 0;

static uint32_t nowSec() { return millis() / 1000; }

// ======================= clock =======================
// Device time = IST wall clock stored as if UTC (FG1 convention). DS1307 if
// fitted and running; otherwise a soft clock set by NTP or the app.
static bool     rtcChip = false;
static uint32_t softBase = 0;         // wall-unix minus uptime seconds

static bool clockValid() { return (rtcChip && rtc.isTimeSet()) || softBase != 0; }
static uint32_t clockUnix() {
    if (rtcChip && rtc.isTimeSet()) return rtc.getUnixTime();
    if (softBase) return softBase + nowSec();
    return 0;
}
static void clockSet(uint32_t wallUnix) {
    softBase = wallUnix - nowSec();
    if (rtcChip) rtc.syncFromUnix(wallUnix);
}
static uint16_t minuteOfDay() { uint32_t u = clockUnix(); return (u % 86400) / 60; }
static String hhmm(uint32_t u) {
    char b[9]; snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", (unsigned long)(u % 86400) / 3600,
                        (unsigned long)(u % 3600) / 60, (unsigned long)u % 60);
    return b;
}

// ======================= event log =======================
struct Event { uint32_t ts; uint32_t up; char msg[72]; };
static Event    events[EVENT_LOG_LEN];
static uint8_t  evHead = 0, evCount = 0;

static void logEvent(const String& msg) {
    Event& e = events[evHead];
    e.ts = clockUnix();
    e.up = nowSec();
    strlcpy(e.msg, msg.c_str(), sizeof(e.msg));
    evHead = (evHead + 1) % EVENT_LOG_LEN;
    if (evCount < EVENT_LOG_LEN) evCount++;
    Serial.printf("[EVENT] %s\n", msg.c_str());
}

static String eventsJson() {
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    for (uint8_t i = 0; i < evCount; i++) {
        const Event& e = events[(evHead + EVENT_LOG_LEN - evCount + i) % EVENT_LOG_LEN];
        JsonObject o = a.add<JsonObject>();
        o["ts"] = e.ts;
        o["up"] = e.up;
        o["msg"] = e.msg;
    }
    String s; serializeJson(d, s); return s;
}

// ======================= 24 h trend (RAM) =======================
struct TrendPt { uint32_t ts; int16_t t, rh, ot, orh; uint16_t pumpSec; };
static TrendPt  trend[TREND_POINTS];
static uint16_t trHead = 0, trCount = 0;
static struct { float t = 0, rh = 0, ot = 0, orh = 0; uint16_t n = 0, on = 0; uint32_t pump = 0; uint32_t slot = 0; } acc;

static void trendTick(const ControlReading& cr, uint32_t now) {
    uint32_t slot = (clockValid() ? clockUnix() : now) / TREND_SLOT_SEC;
    if (acc.slot && slot != acc.slot) {
        TrendPt& p = trend[trHead];
        p.ts = acc.slot * TREND_SLOT_SEC;
        p.t   = acc.n  ? (int16_t)lroundf(acc.t / acc.n * 10) : INT16_MIN;
        p.rh  = acc.n  ? (int16_t)lroundf(acc.rh / acc.n * 10) : INT16_MIN;
        p.ot  = acc.on ? (int16_t)lroundf(acc.ot / acc.on * 10) : INT16_MIN;
        p.orh = acc.on ? (int16_t)lroundf(acc.orh / acc.on * 10) : INT16_MIN;
        p.pumpSec = acc.pump;
        trHead = (trHead + 1) % TREND_POINTS;
        if (trCount < TREND_POINTS) trCount++;
        acc = {};
    }
    acc.slot = slot;
    if (cr.ok) { acc.t += cr.t; acc.rh += cr.rh; acc.n++; }
    if (nodes.localOk(now)) { acc.ot += nodes.localT(now); acc.orh += nodes.localRh(now); acc.on++; }
    if (relay.isOn()) acc.pump++;
}

// [[ts, t, rh, outT, outRh, pumpSec], ...]  values x10, null when missing
static String trendJson() {
    String s; s.reserve(trCount * 34 + 4);
    s = "[";
    auto v = [](int16_t x) { return x == INT16_MIN ? String("null") : String(x); };
    for (uint16_t i = 0; i < trCount; i++) {
        const TrendPt& p = trend[(trHead + TREND_POINTS - trCount + i) % TREND_POINTS];
        if (i) s += ',';
        s += '['; s += p.ts; s += ','; s += v(p.t); s += ','; s += v(p.rh); s += ',';
        s += v(p.ot); s += ','; s += v(p.orh); s += ','; s += p.pumpSec; s += ']';
    }
    s += ']';
    return s;
}

// ======================= daily counters =======================
static uint32_t today = 0, startsToday = 0, onSecToday = 0, pumpOnSince = 0;

// ======================= settings =======================
static void saveSettings() {
    Settings s = mistCtl.settings();
    prefs.putBytes("mist", &s, sizeof(s));
    prefs.putUChar("mistv", 1);
    prefs.putULong("rev", settingsRev);
}
static void loadSettings() {
    Settings s;
    if (prefs.getUChar("mistv", 0) == 1 && prefs.getBytesLength("mist") == sizeof(Settings))
        prefs.getBytes("mist", &s, sizeof(s));
    s.sanitize();
    mistCtl.setSettings(s);
    settingsRev = prefs.getULong("rev", 0);
    // Power restore always comes back in Auto, unless the user had set Off.
    Mode m = (Mode)prefs.getUChar("mode", (uint8_t)Mode::Auto);
    mistCtl.setMode(m == Mode::Off ? Mode::Off : Mode::Auto, 0);
}

static void settingsToJson(JsonObject o) {
    const Settings& s = mistCtl.settings();
    o["t_start"] = s.tStart;  o["t_stop"] = s.tStop;
    o["rh_low"] = s.rhLow;    o["rh_high"] = s.rhHigh;
    o["on_s"] = s.onSec;      o["off_s"] = s.offSec;
    o["win_on"] = s.windowOn; o["win_start"] = s.winStartMin; o["win_end"] = s.winEndMin;
    o["max_starts"] = s.maxStartsHr; o["manual_max"] = s.manualMaxMin;
    o["sensor_timeout"] = nodes.sensorTimeoutSec;
    o["rev"] = settingsRev;
}

static bool applySettings(JsonObject p, String& err, bool fromRetained) {
    uint32_t rev = p["rev"] | 0;
    if (fromRetained && rev && rev <= settingsRev) return true;   // stale retained copy
    Settings s = mistCtl.settings();
    if (p["t_start"].is<float>())  s.tStart = p["t_start"];
    if (p["t_stop"].is<float>())   s.tStop = p["t_stop"];
    if (p["rh_low"].is<float>())   s.rhLow = p["rh_low"];
    if (p["rh_high"].is<float>())  s.rhHigh = p["rh_high"];
    if (p["on_s"].is<int>())       s.onSec = p["on_s"];
    if (p["off_s"].is<int>())      s.offSec = p["off_s"];
    if (p["win_on"].is<bool>())    s.windowOn = p["win_on"];
    if (p["win_start"].is<int>())  s.winStartMin = p["win_start"];
    if (p["win_end"].is<int>())    s.winEndMin = p["win_end"];
    if (p["max_starts"].is<int>()) s.maxStartsHr = p["max_starts"];
    if (p["manual_max"].is<int>()) s.manualMaxMin = p["manual_max"];
    bool clamped = !s.sanitize();   // saved anyway, with out-of-range values pulled into range
    mistCtl.setSettings(s);
    settingsRev = rev ? rev : settingsRev + 1;
    saveSettings();
    char b[96];
    snprintf(b, sizeof(b), "Settings: start %.1f/%.0f%%, stop %.1f/%.0f%%, %us on/%us off, %02u:%02u-%02u:%02u%s",
             s.tStart, s.rhLow, s.tStop, s.rhHigh, s.onSec, s.offSec, s.winStartMin / 60, s.winStartMin % 60,
             s.winEndMin / 60, s.winEndMin % 60, clamped ? " (adjusted)" : s.windowOn ? "" : " (window off)");
    logEvent(b);
    return true;
}

// ======================= WiFi (from FG1) =======================
enum ConnMode { CONN_CLOUD, CONN_LOCAL };
static ConnMode connMode = CONN_LOCAL;
static bool     forcedLocal = false;
static String   apSsid;
static uint32_t lastRetry = 0;
static bool     retrying = false;
static uint32_t retryStart = 0;
static uint32_t downSince = 0, upSince = 0;
static volatile bool scanInProgress = false;
static bool     scanPending = false;
static uint32_t scanStart = 0;
static bool     ntpPending = false;
static uint32_t ntpStart = 0;

static void radioChanged() {
    // Mode switches can drop promiscuous RSSI capture; re-arm it.
    esp_wifi_set_promiscuous(true);
}

static void onWiFiUp() {
    Serial.printf("[WiFi] Connected — %s  IP %s  ch%u\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.channel());
    configTime(19800, 0, "in.pool.ntp.org", "pool.ntp.org");
    ntpPending = true;
    ntpStart = millis();
    connMode = CONN_CLOUD;
}

static void pollNtp() {
    if (!ntpPending) return;
    struct tm t;
    if (getLocalTime(&t, 0)) {
        ntpPending = false;
        if (rtcChip) rtc.syncFromTm(t);
        DateTime dt(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        softBase = dt.unixtime() - nowSec();
        Serial.printf("[NTP] %02d:%02d:%02d IST\n", t.tm_hour, t.tm_min, t.tm_sec);
    } else if (millis() - ntpStart > 30000) {
        ntpPending = false;
        Serial.println("[NTP] failed — keeping RTC time");
    }
}

static bool tryConnect(uint32_t timeoutMs) {
    String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
    if (ssid.isEmpty()) return false;
    Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t s = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - s < timeoutMs) { leds.loop(); delay(20); }
    return WiFi.status() == WL_CONNECTED;
}

static void startLocal() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    apSsid = deviceId;
    WiFi.softAP(apSsid.c_str(), SOFTAP_PASSWORD, SOFTAP_CHANNEL);
    connMode = CONN_LOCAL;
    lastRetry = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
    radioChanged();
    Serial.printf("[WiFi] SoftAP %s at %s, ch%u\n", apSsid.c_str(),
                  WiFi.softAPIP().toString().c_str(), WiFi.channel());
}

static void wifiLoop() {
    bool up = WiFi.status() == WL_CONNECTED;
    if (up) {
        downSince = 0;
        if (!upSince) upSince = millis();
        if (connMode == CONN_LOCAL && !forcedLocal && millis() - upSince >= 60000) {
            Serial.println("[WiFi] Stable 60 s — leaving SoftAP");
            web.closeAll();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            radioChanged();
            onWiFiUp();
        }
    } else {
        upSince = 0;
        if (connMode == CONN_CLOUD) {
            if (!downSince) downSince = millis();
            else if (millis() - downSince >= 30000) {
                Serial.println("[WiFi] Down 30 s — SoftAP fallback");
                logEvent("WiFi lost — local mode (SoftAP)");
                startLocal();
                downSince = 0;
            }
        }
    }
    // Background retry while on SoftAP (non-blocking)
    if (connMode == CONN_LOCAL && !retrying && !up && !scanInProgress && !forcedLocal &&
        !Update.isRunning() && millis() - lastRetry >= WIFI_RETRY_INTERVAL_MS) {
        lastRetry = millis();
        String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
        if (!ssid.isEmpty()) {
            WiFi.begin(ssid.c_str(), pass.c_str());
            retrying = true;
            retryStart = millis();
        }
    }
    if (retrying) {
        if (WiFi.status() == WL_CONNECTED) { retrying = false; Serial.println("[WiFi] Reconnected — confirming"); }
        else if (millis() - retryStart >= 8000) { retrying = false; WiFi.disconnect(false, false); }
    }
    if (scanPending && millis() - scanStart >= 300 && scanner.checkComplete()) {
        scanPending = false;
        scanInProgress = false;
        String j = scanner.resultAsJson();
        web.push("wifi_scan_result", j);
    }
}

static void updateLinkLed() {
    if (connMode == CONN_LOCAL && WiFi.status() != WL_CONNECTED)
        leds.setWiFiState(WiFi.softAPgetStationNum() > 0 ? WIFI_LED_AP_CLIENT : WIFI_LED_AP_IDLE);
    else if (WiFi.status() != WL_CONNECTED) leds.setWiFiState(WIFI_LED_SEARCHING);
    else if (!cloud.connected())             leds.setWiFiState(WIFI_LED_WIFI_ONLY);
    else                                     leds.setWiFiState(WIFI_LED_FULL_OK);
}

// ======================= status =======================
static ControlReading lastCtl;

static String statusJson() {
    uint32_t now = nowSec();
    JsonDocument d;
    d["device_id"] = deviceId;
    d["fw"] = FIRMWARE_VERSION;
    d["uptime"] = now;
    d["clock_ok"] = clockValid();
    d["rtc_chip"] = rtcChip;
    if (clockValid()) { d["unix"] = clockUnix(); d["time"] = hhmm(clockUnix()); }

    d["pump_on"] = relay.isOn();
    d["mode"] = modeText(mistCtl.mode());
    d["reason"] = reasonText(mistCtl.reason());
    d["demand"] = mistCtl.demand();
    d["in_window"] = mistCtl.inWindow();
    d["phase_left"] = mistCtl.phaseRemaining();
    d["starts_hr"] = mistCtl.startsLastHour(now);
    d["starts_today"] = startsToday;
    d["on_min_today"] = (onSecToday + (relay.isOn() ? now - pumpOnSince : 0)) / 60;

    JsonObject c = d["control"].to<JsonObject>();
    c["ok"] = lastCtl.ok;
    if (lastCtl.ok) { c["t"] = serialized(String(lastCtl.t, 1)); c["rh"] = serialized(String(lastCtl.rh, 1)); }
    c["used"] = lastCtl.used;
    c["selected"] = lastCtl.selected;
    c["fallback"] = lastCtl.fallback;

    JsonObject o = d["outside"].to<JsonObject>();
    o["ok"] = nodes.localOk(now);
    o["sensor"] = sensor.name();
    if (nodes.localOk(now)) {
        o["t"] = serialized(String(nodes.localT(now), 1));
        o["rh"] = serialized(String(nodes.localRh(now), 1));
    }

    nodes.toJson(d.as<JsonObject>(), now);

    JsonObject w = d["wifi"].to<JsonObject>();
    w["connected"] = WiFi.status() == WL_CONNECTED;
    w["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : (prefs.isKey("ssid") ? prefs.getString("ssid", "") : String());
    w["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    w["mode"] = connMode == CONN_CLOUD ? "cloud" : "local";
    w["forced_local"] = forcedLocal;
    w["ap"] = (WiFi.getMode() & WIFI_AP) ? apSsid : "";
    w["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    w["channel"] = WiFi.channel();
    d["mqtt"] = cloud.connected();

    settingsToJson(d["settings"].to<JsonObject>());
    String s; serializeJson(d, s);
    return s;
}

static uint32_t lastPublish = 0;
static void publishStatus() {
    lastPublish = millis();
    String s = statusJson();
    web.setStatus(s);
    cloud.publishStatus(s);
}

// ======================= commands =======================
static bool pendingRestart = false, pendingReset = false;
static uint32_t restartAt = 0;

static String handleCommand(const String& cmd, JsonObject p, String& err, bool fromRetained = false) {
    uint32_t now = nowSec();
    if (cmd == "get_status" || cmd == "device_info") return statusJson();

    if (cmd == "set_mode") {
        String m = p["mode"] | "";
        Mode md = m == "off" ? Mode::Off : m == "manual" ? Mode::Manual : Mode::Auto;
        mistCtl.setMode(md, now);
        prefs.putUChar("mode", (uint8_t)md);
        logEvent(String("Mode: ") + modeText(md));
        publishStatus();
        return "{}";
    }
    if (cmd == "pump_on")  { mistCtl.manualOn(now);  logEvent("Manual: pump ON");  return "{}"; }
    if (cmd == "pump_off") { mistCtl.manualOff(now); logEvent("Manual: pump OFF"); return "{}"; }

    if (cmd == "set_settings") { applySettings(p, err, fromRetained); publishStatus(); return "{}"; }
    if (cmd == "get_settings") {
        JsonDocument d; settingsToJson(d.to<JsonObject>());
        String s; serializeJson(d, s); return s;
    }

    if (cmd == "node_add")      { nodes.add(p["mac"] | "", err); return "{}"; }
    if (cmd == "node_remove")   { nodes.remove(p["mac"] | "", err); publishStatus(); return "{}"; }
    if (cmd == "node_rename")   { nodes.rename(p["mac"] | "", p["name"] | "", err); publishStatus(); return "{}"; }
    if (cmd == "node_identify") { nodes.identify(p["mac"] | "", err); return "{}"; }
    if (cmd == "set_control")   { nodes.setControlSources(p["sources"].as<JsonArrayConst>()); publishStatus(); return "{}"; }

    if (cmd == "get_events") return eventsJson();
    if (cmd == "get_trend")  return trendJson();

    if (cmd == "rtc_sync") {
        uint32_t u = p["unix"] | 0;
        if (u < 1700000000UL) { err = "bad time"; return "{}"; }
        bool wasValid = clockValid();
        int32_t drift = wasValid ? (int32_t)(u - clockUnix()) : 0;
        if (!wasValid || abs(drift) > 30) {
            clockSet(u);
            logEvent(wasValid ? "Clock corrected from phone (" + String(drift) + " s)" : "Clock set from phone");
        }
        return "{}";
    }

    if (cmd == "wifi_scan") {
        retrying = false;
        scanInProgress = true;
        scanPending = true;
        scanStart = millis();
        scanner.startScan();
        return "{\"scanning\":true}";
    }
    if (cmd == "wifi_config") {
        prefs.putString("ssid", p["ssid"] | "");
        prefs.putString("pass", p["pass"] | "");
        forcedLocal = false;
        lastRetry = millis() - WIFI_RETRY_INTERVAL_MS;
        logEvent(String("WiFi set to \"") + (p["ssid"] | "") + "\"");
        return "{\"msg\":\"saved, connecting in background\"}";
    }
    if (cmd == "force_local_mode") {
        forcedLocal = true;
        WiFi.disconnect();
        if (connMode != CONN_LOCAL) startLocal();
        return "{}";
    }
    if (cmd == "resume_auto_mode") { forcedLocal = false; lastRetry = millis() - WIFI_RETRY_INTERVAL_MS; return "{}"; }
    if (cmd == "reboot")        { pendingRestart = true; restartAt = millis() + 500; return "{}"; }
    if (cmd == "factory_reset") { pendingReset = true; restartAt = millis() + 500; return "{}"; }

    err = "unknown cmd";
    return "{}";
}

// ======================= setup / loop =======================
static String computeId() {
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    char b[16];
    snprintf(b, sizeof(b), "%s_%02X%02X", DEVICE_PREFIX, m[4], m[5]);
    return b;
}


// Bench check: scan the I2C bus 20 times and count answers per address, so a
// part that answers only sometimes (e.g. DS1307 on low VCC / floating VBAT)
// shows up as e.g. 0x68 12/20.
static void i2cProbe() {
    uint8_t hits[128] = {0};
    for (int n = 0; n < 20; n++) {
        for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) hits[a]++; }
        delay(20);
    }
    Serial.print("[I2C] SDA " + String(I2C_SDA) + " SCL " + String(I2C_SCL) + ":");
    bool any = false;
    for (uint8_t a = 1; a < 127; a++) if (hits[a]) { Serial.printf(" 0x%02X %u/20", a, hits[a]); any = true; }
    Serial.println(any ? "" : " nothing");
}


// Keep the radio TX power capped (RADIO_TX_POWER) across WiFi mode changes.
static void keepTxPower() {
#ifdef RADIO_TX_POWER
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    if (WiFi.getMode() != WIFI_OFF && WiFi.getTxPower() != RADIO_TX_POWER) {
        WiFi.setTxPower(RADIO_TX_POWER);
        Serial.printf("[WiFi] TX power capped at %.1f dBm\n", RADIO_TX_POWER / 4.0);
    }
#endif
}

void setup() {
    Serial.begin(115200);
    uint32_t brownout = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);   // see FG1 main.cpp
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }

    relay.begin(RELAY_PIN);                 // OFF first, before anything else
#ifdef PUMP_OUT_PIN
    pinMode(PUMP_OUT_PIN, OUTPUT);
    digitalWrite(PUMP_OUT_PIN, LOW);           // unused small-pump output stays OFF
#endif
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    leds.begin();
    leds.setWiFiState(WIFI_LED_SEARCHING);
    deviceId = computeId();
    Serial.printf("\n[BOOT] FG1 Polyhouse Controller %s  fw %s\n", deviceId.c_str(), FIRMWARE_VERSION);

    prefs.begin("fg1p", false);
    loadSettings();

    rtc.begin();                            // also starts Wire on 21/22
    Wire.beginTransmission(0x68);
    rtcChip = Wire.endTransmission() == 0;
    if (!rtcChip) Serial.println("[RTC] DS1307 not found — clock comes from NTP / the app");
    sensor.begin();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    espnow.begin();
    nodes.onEvent = [](const String& s) { logEvent(s); };
    nodes.onData = []() { leds.flowPulse(); };
    nodes.begin(&prefs, deviceId);

    if (tryConnect(WIFI_CONNECT_TIMEOUT_MS)) onWiFiUp();
    else startLocal();
    radioChanged();

    web.onCommand = [](const String& c, JsonObject p, String& err) { return handleCommand(c, p, err); };
    web.begin();
    cloud.onCommand = [](const String& c, JsonObject p) {
        String err;
        // set_settings over MQTT may be the broker's retained copy: only
        // apply it if its rev is newer than ours.
        String data = handleCommand(c, p, err, c == "set_settings");
        JsonDocument d;
        d["cmd"] = c; d["ok"] = err.isEmpty();
        if (!p["id"].isNull()) d["id"] = p["id"];
        if (!err.isEmpty()) d["error"] = err;
        String s; serializeJson(d, s);
        s.remove(s.length() - 1);
        s += ",\"data\":" + data + "}";
        cloud.publishResp(s);
    };
    cloud.begin(deviceId);

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brownout);
    logEvent(String("Power on — fw ") + FIRMWARE_VERSION + ", mode " + modeText(mistCtl.mode()));
}

void loop() {
    keepTxPower();
    leds.loop();
    relay.loop();
    web.loop();
    cloud.loop();
    nodes.loop();
    pollNtp();
    wifiLoop();

    uint32_t now = nowSec();

    static uint32_t lastSensor = 0;
    if (millis() - lastSensor >= SENSOR_READ_MS) {
        lastSensor = millis();
        sensor.read();
        nodes.updateLocal(sensor.ok(), sensor.t(), sensor.rh(), now);
    }

    static uint32_t lastTick = 0;
    if (now != lastTick) {
        lastTick = now;
        lastCtl = nodes.control(now);
        Inputs in;
        in.sensorOk = lastCtl.ok;
        in.t = lastCtl.t;
        in.rh = lastCtl.rh;
        in.timeValid = clockValid();
        in.minuteOfDay = minuteOfDay();
        in.nowSec = now;

        bool wasOn = relay.isOn();
        Reason prevReason = mistCtl.reason();
        bool want = mistCtl.update(in);
        if (want) relay.on(); else relay.off();

        if (want != wasOn) {
            char b[72];
            if (want) {
                startsToday++;
                pumpOnSince = now;
                snprintf(b, sizeof(b), "Pump ON (%s) T %.1f RH %.0f%%", reasonText(mistCtl.reason()), in.t, in.rh);
            } else {
                onSecToday += now - pumpOnSince;
                snprintf(b, sizeof(b), "Pump OFF (%s) T %.1f RH %.0f%%", reasonText(mistCtl.reason()), in.t, in.rh);
            }
            logEvent(b);
            publishStatus();
        } else if (mistCtl.reason() != prevReason &&
                   (mistCtl.reason() == Reason::SensorFault || prevReason == Reason::SensorFault ||
                    mistCtl.reason() == Reason::StartLimit)) {
            logEvent(String("State: ") + reasonText(mistCtl.reason()));
        }

        // day rollover
        if (clockValid()) {
            uint32_t day = clockUnix() / 86400;
            if (today && day != today) { startsToday = 0; onSecToday = 0; }
            today = day;
        }
        trendTick(lastCtl, now);
    }

    if (millis() - lastPublish >= STATUS_PUBLISH_MS) publishStatus();

    static uint32_t lastLed = 0;
    if (millis() - lastLed >= 500) { lastLed = millis(); updateLinkLed(); }

    // BOOT held 5 s: forget farm WiFi, restart into SoftAP
    static uint32_t btnDown = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (!btnDown) btnDown = millis();
        else if (millis() - btnDown >= 5000) {
            Serial.println("[BOOT] Button: forgetting WiFi");
            prefs.remove("ssid"); prefs.remove("pass");
            pendingRestart = true; restartAt = millis() + 200; btnDown = 0;
        }
    } else btnDown = 0;

    if (Serial.available()) {
        String c = Serial.readStringUntil('\n'); c.trim();
        String err; JsonDocument d; JsonObject p = d.to<JsonObject>();
        if (c == "status")        Serial.println(statusJson());
        else if (c == "events")   Serial.println(eventsJson());
        else if (c == "pump on")  handleCommand("pump_on", p, err);
        else if (c == "pump off") handleCommand("pump_off", p, err);
        else if (c == "auto")     { p["mode"] = "auto"; handleCommand("set_mode", p, err); }
        else if (c == "manual")   { p["mode"] = "manual"; handleCommand("set_mode", p, err); }
        else if (c == "i2c")      i2cProbe();
        else if (c == "i2cswap") {                  // bench: scan with SDA/SCL exchanged (cable wired backwards?)
            Wire.end(); Wire.begin(I2C_SCL, I2C_SDA);
            Serial.print("[I2C] swapped pins -> "); i2cProbe();
            Wire.end(); Wire.begin(I2C_SDA, I2C_SCL);
        }
        else if (c == "sensor")   { sensor.begin(); Serial.printf("[TH] %s ok=%d\n", sensor.name(), sensor.ok()); }
        else if (c == "rtc") {
            Wire.beginTransmission(0x68); Wire.write(0);
            int e = Wire.endTransmission();
            Serial.printf("[RTC] write ptr err=%d  regs:", e);
            if (e == 0 && Wire.requestFrom(0x68, 8) == 8) while (Wire.available()) Serial.printf(" %02X", Wire.read());
            Serial.printf("\n[RTC] now %s\n", rtc.getTimeString().c_str());
        }
        else if (c.startsWith("cmd ")) {            // bench: any app command, e.g. cmd {"cmd":"node_add","mac":"48:F6:EE:12:6A:F8"}
            JsonDocument in;
            if (deserializeJson(in, c.substring(4))) Serial.println("[CMD] bad JSON");
            else {
                String name = in["cmd"] | "";
                String data = handleCommand(name, in.as<JsonObject>(), err);
                Serial.printf("[CMD] %s -> %s %s\n", name.c_str(), err.length() ? "ERROR" : "ok", data.c_str());
            }
        }
        else if (c.startsWith("settime ")) { p["unix"] = (uint32_t)c.substring(8).toInt(); handleCommand("rtc_sync", p, err); }
        else if (c.startsWith("gpio ")) {           // bench: "gpio 20 1" drive, "gpio 20" read
            int sp = c.indexOf(' ', 5);
            int pin = c.substring(5, sp < 0 ? c.length() : sp).toInt();
            if (sp > 0) { pinMode(pin, OUTPUT); digitalWrite(pin, c.substring(sp + 1).toInt() ? HIGH : LOW); }
            Serial.printf("[GPIO] %d reads %d\n", pin, digitalRead(pin));
        }
        if (err.length()) Serial.println("[CMD] " + err);
    }

    if ((pendingRestart || pendingReset) && (int32_t)(millis() - restartAt) >= 0) {
        relay.off();
        if (pendingReset) { prefs.clear(); }
        ESP.restart();
    }
}
