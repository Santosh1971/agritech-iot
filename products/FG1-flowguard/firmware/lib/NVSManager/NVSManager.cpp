#include "NVSManager.h"
#include <nvs_flash.h>

void NVSManager::begin() {
    // Initialize NVS flash partition
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[NVS] Erasing flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // Create namespace by opening in write mode once
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.end();
    Serial.println("[NVS] Initialized OK");
}

// ---------- Cycles ----------
void NVSManager::saveCycles(Cycle* cycles, uint8_t count) {
    _prefs.begin(NVS_NAMESPACE, false);
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
    _prefs.putString("cycles", out);
    _prefs.putUChar("cycle_count", count);
    _prefs.end();
    Serial.printf("[NVS] Saved %d cycles\n", count);
}

uint8_t NVSManager::loadCycles(Cycle* cycles) {
    _prefs.begin(NVS_NAMESPACE, false);
    uint8_t count = _prefs.getUChar("cycle_count", 0);
    if (count == 0) { _prefs.end(); return 0; }
    String json = _prefs.getString("cycles", "[]");
    _prefs.end();
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return 0;
    JsonArray arr = doc.as<JsonArray>();
    uint8_t i = 0;
    for (JsonObject o : arr) {
        if (i >= MAX_CYCLES) break;
        cycles[i].id           = o["id"];
        strlcpy(cycles[i].name, o["name"] | "", sizeof(cycles[i].name));
        cycles[i].startHour    = o["sh"];
        cycles[i].startMinute  = o["sm"];
        cycles[i].endHour      = o["eh"];
        cycles[i].endMinute    = o["em"];
        cycles[i].mode         = (OperationMode)(int)o["mode"];
        cycles[i].targetLiters = o["liters"];
        cycles[i].enabled      = o["enabled"];
        i++;
    }
    Serial.printf("[NVS] Loaded %d cycles\n", i);
    return i;
}

// ---------- WiFi ----------
void NVSManager::saveWiFi(const char* ssid, const char* pass) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("wifi_ssid", ssid);
    _prefs.putString("wifi_pass", pass);
    _prefs.end();
    Serial.printf("[NVS] WiFi saved: %s\n", ssid);
}

bool NVSManager::loadWiFi(char* ssid, char* pass) {
    _prefs.begin(NVS_NAMESPACE, false);
    String s = _prefs.getString("wifi_ssid", "");
    String p = _prefs.getString("wifi_pass", "");
    _prefs.end();
    if (s.isEmpty()) return false;
    strlcpy(ssid, s.c_str(), 64);
    strlcpy(pass, p.c_str(), 64);
    return true;
}

// ---------- Forced local mode ----------
// Persists across power cycles so a deliberate force_local_mode test
// (Settings -> SoftAP Testing in the app) survives a reboot rather than
// silently reverting to normal auto-reconnect the moment power is lost —
// previously this was only an in-RAM flag.
void NVSManager::saveForcedLocalMode(bool forced) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putBool("forced_local", forced);
    _prefs.end();
}

bool NVSManager::loadForcedLocalMode() {
    _prefs.begin(NVS_NAMESPACE, false);
    bool forced = _prefs.getBool("forced_local", false);
    _prefs.end();
    return forced;
}

// ---------- MQTT ----------
void NVSManager::saveMQTT(const char* broker, uint16_t port,
                           const char* user, const char* pass) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString("mqtt_broker", broker);
    _prefs.putUShort("mqtt_port",   port);
    _prefs.putString("mqtt_user",   user);
    _prefs.putString("mqtt_pass",   pass);
    _prefs.end();
    Serial.printf("[NVS] MQTT saved: %s:%d\n", broker, port);
}

bool NVSManager::loadMQTT(char* broker, uint16_t& port,
                           char* user, char* pass) {
    _prefs.begin(NVS_NAMESPACE, false);
    String b = _prefs.getString("mqtt_broker", "");
    port     = _prefs.getUShort("mqtt_port",   1883);
    String u = _prefs.getString("mqtt_user",   "");
    String p = _prefs.getString("mqtt_pass",   "");
    _prefs.end();
    if (b.isEmpty()) return false;
    strlcpy(broker, b.c_str(), 128);
    strlcpy(user,   u.c_str(), 64);
    strlcpy(pass,   p.c_str(), 64);
    return true;
}

// ---------- Running State ----------
void NVSManager::saveRunningState(const RunningState& s) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putBool("rs_active",   s.active);
    _prefs.putBool("rs_paused",   s.paused);
    _prefs.putUChar("rs_cycleId", s.cycleId);
    _prefs.putFloat("rs_liters",  s.litersDelivered);
    _prefs.putULong("rs_start",   s.startUnix);
    _prefs.putString("rs_by",     s.startedBy);
    _prefs.end();
}

bool NVSManager::loadRunningState(RunningState& s) {
    _prefs.begin(NVS_NAMESPACE, false);
    s.active          = _prefs.getBool("rs_active",   false);
    s.paused          = _prefs.getBool("rs_paused",   false);
    s.cycleId         = _prefs.getUChar("rs_cycleId", 0);
    s.litersDelivered = _prefs.getFloat("rs_liters",  0.0f);
    s.startUnix       = _prefs.getULong("rs_start",   0);
    String by         = _prefs.getString("rs_by",     "auto");
    strlcpy(s.startedBy, by.c_str(), sizeof(s.startedBy));
    _prefs.end();
    return s.active;
}

void NVSManager::clearRunningState() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putBool("rs_active", false);
    _prefs.putBool("rs_paused", false);
    _prefs.end();
}

// ---------- History ----------
// All four methods below share one packed blob (key "history") holding
// the full HISTORY_MAX_ENTRIES-entry circular buffer as raw bytes,
// instead of the previous design (one NVS key per entry, "h_0".."h_199").
// That design was the actual root cause of the NVS exhaustion seen in
// testing: 200 separate keys, each carrying its own NVS bookkeeping
// overhead on top of the JSON string payload, could exceed a small NVS
// partition well before the data itself did. One blob write has a single
// entry's worth of that overhead, regardless of how many logical history
// records it holds.

void NVSManager::_loadHistoryBlob() {
    size_t expected = sizeof(_historyBuf);
    if (_prefs.getBytesLength("history") == expected) {
        _prefs.getBytes("history", _historyBuf, expected);
    } else {
        // First-ever boot (no blob yet) or a size mismatch (e.g.
        // HISTORY_MAX_ENTRIES changed) — start from a clean, zeroed buffer
        // rather than risk reading garbage/misaligned data.
        memset(_historyBuf, 0, expected);
    }
}

void NVSManager::_saveHistoryBlob() {
    _prefs.putBytes("history", _historyBuf, sizeof(_historyBuf));
}

void NVSManager::addHistoryEntry(const HistoryEntry& entry) {
    _prefs.begin(NVS_NAMESPACE, false);
    uint8_t count = _prefs.getUChar("hist_count", 0);
    _loadHistoryBlob();
    _historyBuf[count % HISTORY_MAX_ENTRIES] = entry;
    _saveHistoryBlob();
    _prefs.putUChar("hist_count", count + 1);
    _prefs.end();
}

void NVSManager::clearHistory() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putUChar("hist_count", 0);
    _prefs.end();
    Serial.println("[NVS] History cleared");
}

uint8_t NVSManager::getHistoryInRange(HistoryEntry* entries, uint8_t maxCount,
                                       uint32_t fromTs, uint32_t toTs) {
    _prefs.begin(NVS_NAMESPACE, false);
    uint8_t total     = _prefs.getUChar("hist_count", 0);
    uint8_t available = min(total, (uint8_t)HISTORY_MAX_ENTRIES);
    uint8_t start      = (total > HISTORY_MAX_ENTRIES) ? total % HISTORY_MAX_ENTRIES : 0;
    _loadHistoryBlob();
    uint8_t matched = 0;
    for (uint8_t i = 0; i < available && matched < maxCount; i++) {
        uint8_t idx = (start + i) % HISTORY_MAX_ENTRIES;
        const HistoryEntry& e = _historyBuf[idx];
        if (e.timestamp < fromTs || e.timestamp > toTs) continue;
        entries[matched] = e;
        matched++;
    }
    _prefs.end();
    return matched;
}

uint8_t NVSManager::getHistory(HistoryEntry* entries, uint8_t maxCount) {
    _prefs.begin(NVS_NAMESPACE, false);
    uint8_t total = _prefs.getUChar("hist_count", 0);
    uint8_t fetch = min(total, maxCount);
    uint8_t start = (total > HISTORY_MAX_ENTRIES) ? total % HISTORY_MAX_ENTRIES : 0;
    _loadHistoryBlob();
    for (uint8_t i = 0; i < fetch; i++) {
        uint8_t idx = (start + i) % HISTORY_MAX_ENTRIES;
        entries[i] = _historyBuf[idx];
    }
    _prefs.end();
    return fetch;
}

// ---------- Calibration ----------
void NVSManager::saveCalibration(uint32_t ppl) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putULong("flow_cal", ppl);
    _prefs.end();
}

uint32_t NVSManager::loadCalibration() {
    _prefs.begin(NVS_NAMESPACE, false);
    uint32_t val = _prefs.getULong("flow_cal", DEFAULT_PULSES_PER_LITER);
    _prefs.end();
    return val;
}

// ---------- Factory Reset ----------
void NVSManager::factoryReset() {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear();
    _prefs.end();
    Serial.println("[NVS] Factory reset done");
}
