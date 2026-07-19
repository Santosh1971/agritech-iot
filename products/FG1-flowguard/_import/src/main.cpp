#include <Arduino.h>
#include <ArduinoJson.h>
#include <nvs_flash.h>
#include "Config.h"
#include "RTCManager.h"
#include "FlowSensor.h"
#include "RelayControl.h"
#include "NVSManager.h"
#include "Scheduler.h"
#include "LocalServer.h"
#include "MQTTClient.h"
#include "LEDManager.h"
#include "WiFiScanner.h"

RTCManager   rtc;
FlowSensor   flowSensor;
RelayControl relay;
NVSManager   nvs;
Scheduler    scheduler;
LocalServer  localServer;
MQTTHandler  mqtt;
LEDManager   leds;
WiFiScanner  wifiScanner;

uint32_t lastStatusPublish = 0;
bool     lastPumpState     = false;
uint32_t lastWiFiCheck     = 0;
uint32_t lastWiFiRetry     = 0;
enum ConnMode { CONN_UNKNOWN, CONN_CLOUD, CONN_LOCAL_FALLBACK };
ConnMode connMode = CONN_UNKNOWN;
String   apSsid;   // set once startLocalFallback() runs — MAC-suffixed, unique per device
String   deviceId;  // computed once in setup() — same MAC-suffixed value used for apSsid
String   macSuffix; // last 4 chars of deviceId — used to scope MQTT topics per-device
// Set by the force_local_mode command — lets someone deliberately test
// SoftAP mode (e.g. "how does this behave locally?") without needing to
// actually turn off their router. While true, the normal automatic
// reconnect/leave-fallback logic is suppressed; resume_auto_mode (or a
// power cycle) turns it back off.
bool     forcedLocalMode = false;

// Non-blocking background WiFi retry state (used only for the periodic
// retry while in local fallback — the one-time boot attempt in setup()
// is still a short blocking wait, which is fine since nothing else is
// running yet at that point).
enum RetryState { RETRY_IDLE, RETRY_CONNECTING };
RetryState retryState     = RETRY_IDLE;
// Set around an explicit wifi_scan — blocks the background retry from
// starting a NEW WiFi.begin() attempt while a scan is in flight. Needed
// because LocalServer command handling and loop() run on different
// ESP32 cores, so just waiting before the scan (avoiding a retry already
// in progress) wasn't enough — a fresh retry could still start mid-scan.
// volatile since it's read/written across those two task contexts.
volatile bool wifiScanInProgress = false;
uint32_t   retryStartMs   = 0;
const uint32_t RETRY_CONNECT_TIMEOUT_MS = 8000;

// Hysteresis for cloud <-> local-fallback transitions, so a brief WiFi
// blip doesn't flap the SoftAP on/off:
//   - Only drop from cloud to fallback after WIFI_DROP_GRACE_MS of being
//     continuously disconnected.
//   - Only leave fallback (tear down the SoftAP) after WIFI_STABLE_HOLD_MS
//     of the reconnected WiFi being continuously up.
const uint32_t WIFI_DROP_GRACE_MS  = 30000;
const uint32_t WIFI_STABLE_HOLD_MS = 60000;
uint32_t wifiDownSinceMs = 0;
uint32_t wifiUpSinceMs   = 0;

// Non-blocking factory reset — HTTP response needs to flush before restart.
bool     pendingFactoryReset   = false;
uint32_t pendingFactoryResetAt = 0;

void IRAM_ATTR flowISR() {
    flowSensor.pulseISR();
    leds.flowPulse();
}

String buildStatusJSON() {
    JsonDocument doc;
    doc["device_id"]        = deviceId;
    doc["firmware"]         = FIRMWARE_VERSION;
    doc["pump_on"]          = relay.isOn();
    doc["rtc_time"]         = rtc.getTimeString();
    doc["rtc_date"]         = rtc.getDateString();
    doc["rtc_set"]          = rtc.isTimeSet();
    doc["wifi_rssi"]        = WiFi.RSSI();
    doc["wifi_connected"]   = (WiFi.status() == WL_CONNECTED);
    doc["mqtt_connected"]   = mqtt.isConnected();
    doc["conn_mode"]        = (connMode == CONN_CLOUD) ? "cloud" : "local_fallback";
    doc["forced_local_mode"] = forcedLocalMode;
    doc["ap_ssid"]           = apSsid;
    doc["ap_active"]         = (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_AP);
    doc["local_clients"]     = localServer.isConnected();
    RunningState s          = scheduler.getCurrentState();
    doc["cycle_active"]     = s.active;
    doc["cycle_paused"]     = s.paused;
    doc["cycle_id"]         = s.cycleId;
    doc["liters_delivered"] = s.litersDelivered;
    doc["started_by"]       = s.startedBy;
    doc["cycle_start_unix"] = s.startUnix;
    String out; serializeJson(doc, out);
    return out;
}

// Build cycles JSON from NVS
String buildCyclesJSON() {
    Cycle cycles[MAX_CYCLES];
    uint8_t count = nvs.loadCycles(cycles);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]      = cycles[i].id;
        o["name"]    = cycles[i].name;
        o["sh"]      = cycles[i].startHour;
        o["sm"]      = cycles[i].startMinute;
        o["eh"]      = cycles[i].endHour;
        o["em"]      = cycles[i].endMinute;
        o["mode"]    = (int)cycles[i].mode;
        o["liters"]  = cycles[i].targetLiters;
        o["enabled"] = cycles[i].enabled;
    }
    String out; serializeJson(doc, out);
    return out;
}

void updateWiFiLED() {
    if (WiFi.status() != WL_CONNECTED)    leds.setWiFiState(WIFI_LED_SEARCHING);
    else if (!mqtt.isConnected())          leds.setWiFiState(WIFI_LED_WIFI_ONLY);
    else                                   leds.setWiFiState(WIFI_LED_FULL_OK);
}

// Sync RTC from NTP (IST = UTC+5:30 = 19800 seconds)
void syncNTP() {
    configTime(19800, 0, "in.pool.ntp.org", "asia.pool.ntp.org", "pool.ntp.org");
    Serial.println("[NTP] Syncing time...");
    struct tm timeinfo;
    bool ntpOk = false;
    for (int attempt = 0; attempt < 3 && !ntpOk; attempt++) {
        if (attempt > 0) { delay(2000); Serial.println("[NTP] Retrying..."); }
        ntpOk = getLocalTime(&timeinfo, 8000);
    }
    if (ntpOk) {
        rtc.syncFromTm(timeinfo);
        Serial.printf("[NTP] IST: %02d/%02d/%04d %02d:%02d:%02d\n",
            timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("[NTP] Sync failed — using existing RTC time (fine if this device rarely sees internet)");
    }
}

// Shared success handler — called both on initial boot connect and on
// background recovery from local fallback mode.
void onWiFiConnected() {
    Serial.printf("[WiFi] Connected — IP: %s\n",
        WiFi.localIP().toString().c_str());
    syncNTP();
    connMode = CONN_CLOUD;
}

// Blocking — only ever called from setup() for the one-time boot attempt,
// when nothing else needs the CPU yet.
bool tryConnectSTA(uint32_t timeoutMs) {
    char ssid[64], pass[64];
    if (!nvs.loadWiFi(ssid, pass)) return false;
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    // Service the LED state machine during this wait instead of a blind
    // delay() — otherwise WiFi/sensor LEDs sit frozen at whatever state
    // they were in for up to timeoutMs (15s) right at power-on, only
    // starting to actually blink once this function returns and loop()
    // begins. leds.loop() is cheap and just checks millis()-based timing
    // internally, so calling it frequently here is fine.
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        leds.loop();
        delay(20);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Derives a short unique suffix from the MAC so multiple physical devices
// are distinguishable — used for the SoftAP name, the MQTT client ID and
// topics, and the status JSON's device_id field, all built from this same
// value so they visibly correlate (e.g. AP "SWC_001_A1B2" <-> device_id
// "SWC_001_A1B2"). Previously DEVICE_ID was a fixed "SWC_001" for every
// unit, which — beyond just being unhelpful in the app UI — meant any two
// devices both in Cloud mode would collide on the exact same MQTT topics.
String computeDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    return String(DEVICE_ID) + "_" + suffix;
}

// Starts the device's own WiFi hotspot for local control when no field
// WiFi is reachable (or none configured yet). AP+STA concurrent so
// background STA retries (see loop()) don't disrupt anyone connected
// locally.
void startLocalFallback() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    apSsid = deviceId;
    WiFi.softAP(apSsid.c_str(), SOFTAP_PASSWORD);
    connMode = CONN_LOCAL_FALLBACK;
    lastWiFiRetry = millis();
    Serial.printf("[WiFi] Local fallback active — SSID: %s  IP: %s\n",
        apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

// Kicks off a non-blocking background reconnect attempt. Call
// pollBackgroundRetry() every loop() iteration to check on it.
void beginBackgroundRetry() {
    char ssid[64], pass[64];
    if (!nvs.loadWiFi(ssid, pass)) return;  // nothing saved yet — don't bother
    Serial.println("[WiFi] Local fallback active — retrying saved WiFi (non-blocking)...");
    WiFi.begin(ssid, pass);
    retryState   = RETRY_CONNECTING;
    retryStartMs = millis();
}

void pollBackgroundRetry() {
    if (retryState != RETRY_CONNECTING) return;
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Reconnected — confirming stability before leaving fallback");
        retryState = RETRY_IDLE;
        localServer.closeAllClients();
        // AP stays up here — updateConnMode() tears it down once the
        // connection has held for WIFI_STABLE_HOLD_MS, avoiding flapping
        // if the reconnect is itself flaky.
    } else if (millis() - retryStartMs >= RETRY_CONNECT_TIMEOUT_MS) {
        Serial.println("[WiFi] Retry timed out — staying in local fallback");
        retryState = RETRY_IDLE;
    }
}

// Watches WiFi.status() every loop() tick and drives cloud <-> fallback
// transitions with hysteresis in both directions (see constants above).
// This is what makes the fallback work for an "internet" customer too —
// not just at boot, but any time the connection drops mid-operation.
void updateConnMode() {
    bool staUp = (WiFi.status() == WL_CONNECTED);

    if (staUp) {
        wifiDownSinceMs = 0;
        if (wifiUpSinceMs == 0) wifiUpSinceMs = millis();

        if (connMode == CONN_LOCAL_FALLBACK && !forcedLocalMode &&
            millis() - wifiUpSinceMs >= WIFI_STABLE_HOLD_MS) {
            Serial.println("[WiFi] Stable for 60s — leaving local fallback, dropping SoftAP");
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            onWiFiConnected();
        }
    } else {
        wifiUpSinceMs = 0;
        if (connMode == CONN_CLOUD) {
            if (wifiDownSinceMs == 0) {
                wifiDownSinceMs = millis();
            } else if (millis() - wifiDownSinceMs >= WIFI_DROP_GRACE_MS) {
                Serial.println("[WiFi] Down for 30s+ — entering local fallback");
                startLocalFallback();
                wifiDownSinceMs = 0;
            }
        }
        // connMode == CONN_LOCAL_FALLBACK: periodic retry is driven
        // separately by beginBackgroundRetry()/pollBackgroundRetry() below.
    }
}

// Shared by both transports: MQTT (cloud) and LocalServer (local HTTP/WS).
// Returns a JSON payload string — MQTT's caller ignores it (MQTT already
// publishes explicitly inside the handlers below where relevant);
// LocalServer's caller returns it straight to the requesting client.
String handleCommand(const String& cmd, const JsonObject& payload) {
    Serial.printf("[CMD] %s\n", cmd.c_str());

    // ---------- Operational (irrigation control) ----------
    if (cmd == "stop_cycle")    { scheduler.stopCycle(); return "{}"; }
    if (cmd == "pause_cycle")   { scheduler.pauseCycle(); return "{}"; }
    if (cmd == "resume_cycle")  { scheduler.resumeCycle(); return "{}"; }
    if (cmd == "manual_on")     { scheduler.startManual(); return "{}"; }
    if (cmd == "manual_off")    { scheduler.stopCycle(STOP_USER); return "{}"; }
    if (cmd == "manual_liters") { scheduler.startManual(payload["liters"]); return "{}"; }

    if (cmd == "set_cycles") {
        JsonArray arr = payload["cycles"].as<JsonArray>();
        Cycle cycles[MAX_CYCLES];
        uint8_t count = 0;
        for (JsonObject o : arr) {
            if (count >= MAX_CYCLES) break;
            cycles[count].id           = o["id"];
            strlcpy(cycles[count].name, o["name"] | "", 32);
            cycles[count].startHour    = o["sh"];
            cycles[count].startMinute  = o["sm"];
            cycles[count].endHour      = o["eh"];
            cycles[count].endMinute    = o["em"];
            cycles[count].mode         = (OperationMode)(int)o["mode"];
            cycles[count].targetLiters = (float)o["liters"];
            cycles[count].enabled      = o["enabled"];
            count++;
        }
        nvs.saveCycles(cycles, count);
        Serial.printf("[CMD] Saved %d cycles\n", count);
        // reloadCycles(), NOT begin() — begin() resets running state
        // (memset _state) without turning the relay off first, which
        // would orphan the pump ON if cycles are edited while a manual
        // run or scheduled cycle is active. reloadCycles() only refreshes
        // the cycle list, leaving any in-progress run untouched.
        scheduler.reloadCycles();
        String json = buildCyclesJSON();
        mqtt.publishCycles(json);
        localServer.publishCycles(json);
        return json;
    }

    if (cmd == "get_cycles") {
        String json = buildCyclesJSON();
        mqtt.publishCycles(json);
        localServer.publishCycles(json);
        return json;
    }

    if (cmd == "clear_history") { nvs.clearHistory(); return "{}"; }

    if (cmd == "seed_test_history") {
        // TEST DATA ONLY — generates realistic-looking history across the
        // last 30 days for graph/UI testing. Not for production use.
        nvs.clearHistory();
        uint32_t nowTs = rtc.getUnixTime();
        for (int day = 29; day >= 0; day--) {
            int entriesForDay = random(0, 3);
            for (int e = 0; e < entriesForDay; e++) {
                HistoryEntry h;
                h.timestamp = nowTs - (uint32_t)day * 86400 - random(0, 20000);
                bool isManual = random(0, 2) == 0;
                h.cycleId = isManual ? 255 : 1;
                strlcpy(h.cycleName, isManual ? "" : "Morning Watering", 32);
                h.mode = TIME_BASED;
                h.litersDelivered = 1.0f + (random(0, 700) / 100.0f);
                h.durationSeconds = random(10, 180);
                strlcpy(h.status, isManual ? "manual" : "completed", 16);
                nvs.addHistoryEntry(h);
            }
        }
        Serial.println("[HISTORY] Seeded 30 days of test data");
        return "{}";
    }

    if (cmd == "get_history_range") {
        uint32_t fromTs = payload["from"];
        uint32_t toTs   = payload["to"];
        static HistoryEntry rangeEntries[HISTORY_MAX_ENTRIES];  // static — too large for stack
        uint8_t count = nvs.getHistoryInRange(rangeEntries, HISTORY_MAX_ENTRIES, fromTs, toTs);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (uint8_t i = 0; i < count; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ts"]     = rangeEntries[i].timestamp;
            o["cid"]    = rangeEntries[i].cycleId;
            o["name"]   = rangeEntries[i].cycleName;
            o["mode"]   = (int)rangeEntries[i].mode;
            o["liters"] = rangeEntries[i].litersDelivered;
            o["dur"]    = rangeEntries[i].durationSeconds;
            o["status"] = rangeEntries[i].status;
        }
        String out; serializeJson(doc, out);
        // Only worth attempting if actually connected — otherwise this is
        // a guaranteed failure that still burns ~200ms in retry delays
        // for nothing (seen in testing: every history request while in
        // SoftAP mode was paying this cost even though no one's
        // listening on MQTT at all in that state).
        if (mqtt.isConnected() && !mqtt.publishHistory(out))
            Serial.printf("[MQTT] publishHistory gave up after 3 attempts (%d bytes, "
                           "%d entries, final state=%d)\n", out.length(), count, mqtt.state());
        localServer.publishHistory(out);
        return out;
    }

    if (cmd == "get_history") {
        HistoryEntry entries[20];
        uint8_t count = nvs.getHistory(entries, 20);
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (uint8_t i = 0; i < count; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ts"]     = entries[i].timestamp;
            o["cid"]    = entries[i].cycleId;
            o["name"]   = entries[i].cycleName;
            o["mode"]   = (int)entries[i].mode;
            o["liters"] = entries[i].litersDelivered;
            o["dur"]    = entries[i].durationSeconds;
            o["status"] = entries[i].status;
        }
        String out; serializeJson(doc, out);
        if (mqtt.isConnected() && !mqtt.publishHistory(out))
            Serial.printf("[MQTT] publishHistory gave up after 3 attempts (%d bytes, "
                           "%d entries, final state=%d)\n", out.length(), count, mqtt.state());
        localServer.publishHistory(out);
        return out;
    }

    // ---------- Provisioning (formerly BLE) ----------
    if (cmd == "wifi_config") {
        const char* ssid = payload["ssid"] | "";
        const char* pass = payload["pass"] | "";
        nvs.saveWiFi(ssid, pass);
        Serial.printf("[WiFi] Credentials saved for: %s — retry scheduled\n", ssid);
        // Non-blocking: just fast-forward the retry timer so the existing
        // background retry logic (loop()) picks it up on the next tick,
        // rather than blocking this request while we attempt to connect.
        lastWiFiRetry = millis() - WIFI_RETRY_INTERVAL_MS;
        return "{\"msg\":\"saved, connecting in background\"}";
    }

    if (cmd == "mqtt_config") {
        nvs.saveMQTT(payload["broker"] | "", payload["port"] | 1883,
                     payload["user"]   | "", payload["pass"] | "");
        Serial.println("[MQTT] Config saved — reboot to apply");
        return "{\"msg\":\"saved — reboot to apply\"}";
    }

    if (cmd == "rtc_sync") {
        rtc.syncFromUnix((uint32_t)payload["unix"]);
        return "{}";
    }

    if (cmd == "calibrate") {
        uint32_t ppl = (uint32_t)payload["ppl"];
        flowSensor.setCalibration(ppl);
        nvs.saveCalibration(ppl);
        return "{}";
    }

    if (cmd == "relay_test") {
        relay.testPulse(5000);  // non-blocking — see RelayControl
        return "{}";
    }

    if (cmd == "factory_reset") {
        // Delay the actual reset so the HTTP/WS response can flush first —
        // handled non-blocking in loop().
        pendingFactoryReset   = true;
        pendingFactoryResetAt = millis() + 500;
        return "{}";
    }

    if (cmd == "wifi_scan") {
        // Wait out any retry already in flight — beginBackgroundRetry()'s
        // WiFi.begin() call occupies the radio for up to
        // RETRY_CONNECT_TIMEOUT_MS, and scanning at the same time
        // reliably returns WIFI_SCAN_RUNNING (-2) rather than real
        // results (seen repeatedly in testing — a short retry loop alone
        // wasn't enough).
        uint32_t waitStart = millis();
        while (retryState == RETRY_CONNECTING &&
               millis() - waitStart < RETRY_CONNECT_TIMEOUT_MS) {
            delay(100);
            pollBackgroundRetry();
        }
        // Block a NEW retry from starting while the scan itself runs —
        // loop() (different core) would otherwise happily start one
        // mid-scan.
        wifiScanInProgress = true;
        String result = wifiScanner.scanAsJson();
        wifiScanInProgress = false;
        return result;
    }
    if (cmd == "device_info") return buildStatusJSON();

    if (cmd == "force_local_mode") {
        // Deliberately test SoftAP mode without touching the router —
        // disconnects STA and enters fallback immediately (skipping the
        // normal 30s grace, since this is an explicit request, not a
        // detected drop), and suppresses auto-reconnect until told
        // otherwise. Persisted to NVS so this survives a power cycle too
        // — previously an in-RAM-only flag meant a reboot silently
        // reverted to normal behavior mid-test.
        Serial.println("[WiFi] force_local_mode — entering SoftAP on purpose");
        forcedLocalMode = true;
        nvs.saveForcedLocalMode(true);
        WiFi.disconnect();
        if (connMode != CONN_LOCAL_FALLBACK) startLocalFallback();
        return "{\"msg\":\"forced into local mode\"}";
    }

    if (cmd == "resume_auto_mode") {
        // Turns normal automatic reconnect/hysteresis back on — the
        // background retry will pick up the saved WiFi again on its own
        // next cycle (or immediately, since this resets the retry timer).
        Serial.println("[WiFi] resume_auto_mode — normal reconnect behavior restored");
        forcedLocalMode = false;
        nvs.saveForcedLocalMode(false);
        lastWiFiRetry = millis() - WIFI_RETRY_INTERVAL_MS;
        return "{\"msg\":\"resuming normal auto reconnect\"}";
    }

    return "{\"error\":\"unknown cmd\"}";
}

// Shared by both the event-driven trigger (pump state just changed) and
// the periodic heartbeat (loop()) — one place that actually builds and
// sends the status, so they can't drift out of sync with each other.
void publishStatusNow() {
    lastStatusPublish = millis();
    String status = buildStatusJSON();
    localServer.publishStatus(status);
    if (scheduler.getCurrentState().active)
        localServer.publishActiveCycle(status);
    if (mqtt.isConnected()) {
        mqtt.publishStatus(status);
        if (scheduler.getCurrentState().active)
            mqtt.publishActiveCycle(status);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] SmartWaterController starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    leds.begin();
    leds.setWiFiState(WIFI_LED_SEARCHING);
    nvs.begin();
    rtc.begin();
    relay.begin(RELAY_PIN, RELAY_LED_PIN);
    flowSensor.begin(FLOW_SENSOR_PIN);
    flowSensor.setCalibration(nvs.loadCalibration());
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowISR, RISING);
    scheduler.begin(&nvs, &rtc, &flowSensor, &relay);
    scheduler.checkPowerRecovery();

    // WiFi — connect if credentials exist, else fall back to local SoftAP
    // so the device is still controllable with no field WiFi reachable
    // (or none ever configured). This must come BEFORE localServer.begin():
    // AsyncWebServer/AsyncWebSocket touch lwIP's TCP/IP task immediately on
    // begin(), and that task doesn't exist until WiFi.mode() has run once —
    // starting the local server first crashes with "Invalid mbox".
    WiFi.mode(WIFI_STA);
    // Disable WiFi power-save (modem sleep). ESP32's default power-save
    // behavior is a well-documented cause of periodic STA disconnects on
    // many routers/APs (missed beacons while the radio sleeps) — a very
    // strong match for the recurring ~60s reconnect-loop pattern seen
    // repeatedly in testing, which never happened over SoftAP (no
    // power-save involved there at all). This is the leading fix for
    // that long-standing mystery.
    WiFi.setSleep(false);
    deviceId = computeDeviceId();
    macSuffix = deviceId.substring(deviceId.length() - 4);
    Serial.printf("[BOOT] Device ID: %s\n", deviceId.c_str());

    if (nvs.loadForcedLocalMode()) {
        // A previous force_local_mode test is still active across this
        // power cycle — go straight to SoftAP without even attempting
        // STA, matching what the user explicitly asked for last time.
        Serial.println("[BOOT] Forced local mode persisted — staying in SoftAP");
        forcedLocalMode = true;
        startLocalFallback();
    } else if (tryConnectSTA(WIFI_CONNECT_TIMEOUT_MS)) {
        onWiFiConnected();
    } else {
        Serial.println("[WiFi] Could not connect — starting local fallback (SoftAP)");
        startLocalFallback();
    }

    // Local HTTP+WS server — always on, regardless of AP/STA state, so the
    // device is reachable locally whether it's mid-provisioning, in local
    // fallback, or even over the same LAN as a connected home WiFi.
    localServer.onCommand = [](const String& cmd, const JsonObject& payload) -> String {
        return handleCommand(cmd, payload);
    };
    localServer.begin();

    mqtt.onCommand = [](const String& cmd, const JsonObject& payload) {
        handleCommand(cmd, payload);  // return value unused on the MQTT path
    };

    // Load MQTT config from NVS (set via wifi_config/mqtt_config commands);
    // fall back to Config.h defaults on first boot.
    char mqttBroker[128], mqttUser[64], mqttPass[64];
    uint16_t mqttPort;
    if (nvs.loadMQTT(mqttBroker, mqttPort, mqttUser, mqttPass) && strlen(mqttBroker) > 0) {
        Serial.printf("[MQTT] Using saved config: %s:%d\n", mqttBroker, mqttPort);
        mqtt.begin(mqttBroker, mqttPort, mqttUser, mqttPass, macSuffix);
    } else {
        Serial.println("[MQTT] No saved config — using Config.h defaults");
        mqtt.begin(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS, macSuffix);
    }
    Serial.println("[BOOT] Setup complete");
}

void loop() {
    leds.loop();
    relay.loop();
    localServer.loop();
    mqtt.loop();
    scheduler.loop();

    if (pendingFactoryReset && millis() >= pendingFactoryResetAt) {
        nvs.factoryReset();
        ESP.restart();
    }

    if (millis() - lastWiFiCheck >= 2000) {
        lastWiFiCheck = millis();
        updateWiFiLED();
    }

    updateConnMode();

    // While in local fallback, periodically retry the saved WiFi in the
    // background — fully non-blocking, so LEDs/local WS/scheduler keep
    // running normally during a retry attempt instead of freezing for
    // up to RETRY_CONNECT_TIMEOUT_MS every cycle.
    if (connMode == CONN_LOCAL_FALLBACK && retryState == RETRY_IDLE &&
        !wifiScanInProgress && !forcedLocalMode &&
        millis() - lastWiFiRetry >= WIFI_RETRY_INTERVAL_MS) {
        lastWiFiRetry = millis();
        beginBackgroundRetry();
    }
    pollBackgroundRetry();

    // Immediate push the instant the pump actually changes state, instead
    // of waiting for the next periodic tick — that's what was causing the
    // ~5-6s lag between the relay actually switching and the app finding
    // out (the physical action itself was never delayed, only the status
    // update reaching the app was).
    bool pumpNow = relay.isOn();
    if (pumpNow != lastPumpState) {
        lastPumpState = pumpNow;
        publishStatusNow();
    } else if (millis() - lastStatusPublish >= STATUS_PUBLISH_INTERVAL_MS) {
        publishStatusNow();
    }
}
