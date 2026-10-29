#include "WiFiScanner.h"
#include <ArduinoJson.h>

// Arduino's WiFiScanClass declares a scan FAILED once it has run longer
// than max_ms_per_chan * 20 (10s at our 500ms dwell) — but the radio is
// still scanning. While STA is associated the driver hops back to the
// home channel between scan channels, so a full 13-channel scan
// regularly takes longer than that. Bench-seen 2026-10-16 (Avinash's
// "Scan for Networks" complaint): every scan hit the 10s timeout, the
// retry couldn't start over the still-running driver scan, and the app
// got an empty list every time. _scanTimeout is protected static, so a
// derived class is the supported way to widen it after scanNetworks().
struct ScanTimeoutAccess : public WiFiScanClass {
    static void set(uint32_t ms) { _scanTimeout = ms; }
};

static const uint32_t SCAN_DWELL_MS   = 500;
static const uint32_t SCAN_TIMEOUT_MS = 30000;

static void kickOffScan() {
    // Default per-channel dwell (~120ms) is often too short to catch
    // other APs' beacon frames while concurrently running our own
    // SoftAP — confirmed on real hardware (scan technically succeeded
    // but consistently found 0 networks). Explicit params: async=true,
    // show_hidden=false, passive=false (active probe — faster and more
    // reliable than passive for this case), max_ms_per_chan=500 (up
    // from the ~120ms default). Retries use the same params.
    WiFi.scanNetworks(true, false, false, SCAN_DWELL_MS);
    ScanTimeoutAccess::set(SCAN_TIMEOUT_MS);
}

// While the STA is NOT associated but still trying to be -- a saved SSID
// that isn't in range (NO_AP_FOUND) -- the Arduino core's auto-reconnect
// re-issues WiFi.begin() every ~3s forever (WiFiGeneric.cpp: DoReconnect
// on any reconnectable reason). esp_wifi_scan_start() is refused outright
// while the STA is mid-connect, so scanNetworks() fails instantly with
// WIFI_SCAN_FAILED (-2) every time, no matter how often it's retried.
// Reported by Avinash 2026-10-19 on fw 1.3.0 (FG1-003): logs showed
// "Scan found -2 networks (0 listed) in 312 ms" next to a NO_AP_FOUND
// every 3s -- the 1.3.0 timeout fix only covered scans that start and run
// long, not scans that can't start at all.
//
// So: if we're not associated, stop the connect attempts (and switch
// auto-reconnect off) for the duration of the scan. A voluntary
// disconnect() raises ASSOC_LEAVE, which the core never reconnects on.
// An associated STA is left alone -- scanning while connected to the
// cloud WiFi must not drop that link.
void WiFiScanner::freeRadioForScan() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (!_autoReconnectOverridden) {
        _autoReconnectSaved = WiFi.getAutoReconnect();
        _autoReconnectOverridden = true;
        WiFi.setAutoReconnect(false);
    }
    WiFi.disconnect(false, false);   // keep STA enabled, saved config untouched
}

void WiFiScanner::restoreRadioAfterScan() {
    if (!_autoReconnectOverridden) return;
    WiFi.setAutoReconnect(_autoReconnectSaved);
    _autoReconnectOverridden = false;
}

void WiFiScanner::startScan() {
    Serial.println("[WiFi] Scanning networks (async)...");
    _lastFound = WIFI_SCAN_RUNNING;
    _retries = 0;
    _startMs = millis();
    freeRadioForScan();
    kickOffScan();
    _lastKickMs = millis();
}

bool WiFiScanner::checkComplete() {
    int result = WiFi.scanComplete();

    if (result == WIFI_SCAN_RUNNING) {
        _lastFound = result;
        return false;
    }

    if (result == WIFI_SCAN_FAILED && _retries < MAX_RETRIES) {
        if (millis() - _lastKickMs < RETRY_BACKOFF_MS) return false;   // pace the retries
        _retries++;
        Serial.printf("[WiFi] Scan failed — retrying (%d/%d)...\n", _retries, MAX_RETRIES);
        freeRadioForScan();   // the STA may have started reconnecting again
        kickOffScan();
        _lastKickMs = millis();
        _lastFound = WIFI_SCAN_RUNNING;
        return false;
    }

    // Either a real result (>=0) or we've exhausted retries on repeated
    // failure — either way, this is final.
    _lastFound = result;
    return true;
}

String WiFiScanner::resultAsJson() {
    // Reuse the count from checkComplete()'s read — do NOT call
    // WiFi.scanComplete() again here, since the driver's internal state
    // can shift between two separate calls in concurrent AP+STA mode.
    int found = _lastFound;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    int listed = 0;
    for (int i = 0; i < found && listed < 15; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;   // hidden network — can't be picked in the app anyway
        // Mesh/multi-AP routers show the same SSID several times; keep
        // one entry per name with the strongest signal so the list isn't
        // cluttered with duplicates.
        int32_t rssi = WiFi.RSSI(i);
        bool dup = false;
        for (JsonObject o : arr) {
            if (ssid == o["ssid"].as<const char*>()) {
                if (rssi > o["rssi"].as<int32_t>()) o["rssi"] = rssi;
                dup = true;
                break;
            }
        }
        if (dup) continue;
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = ssid;
        o["rssi"] = rssi;
        o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        listed++;
    }
    WiFi.scanDelete();
    restoreRadioAfterScan();
    if (found < 0) {
        // WIFI_SCAN_FAILED is -2 -- NOT "two networks". The old wording
        // ("found -2 networks") was misread as a successful 2-network scan.
        Serial.printf("[WiFi] Scan FAILED (code %d) after %d retries, %lu ms -- 0 networks returned\n",
                      found, _retries, (unsigned long)(millis() - _startMs));
    } else {
        Serial.printf("[WiFi] Scan found %d networks (%d listed) in %lu ms\n",
                      found, listed, (unsigned long)(millis() - _startMs));
    }
    String out; serializeJson(doc, out);
    return out;
}
