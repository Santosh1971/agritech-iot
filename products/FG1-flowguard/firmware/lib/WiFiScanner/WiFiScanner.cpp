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

void WiFiScanner::startScan() {
    Serial.println("[WiFi] Scanning networks (async)...");
    _lastFound = WIFI_SCAN_RUNNING;
    _retries = 0;
    _startMs = millis();
    kickOffScan();
}

bool WiFiScanner::checkComplete() {
    int result = WiFi.scanComplete();

    if (result == WIFI_SCAN_RUNNING) {
        _lastFound = result;
        return false;
    }

    if (result == WIFI_SCAN_FAILED && _retries < MAX_RETRIES) {
        _retries++;
        Serial.printf("[WiFi] Scan failed — retrying (%d/%d)...\n", _retries, MAX_RETRIES);
        kickOffScan();
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
    Serial.printf("[WiFi] Scan found %d networks (%d listed) in %lu ms\n",
                  found, listed, (unsigned long)(millis() - _startMs));
    String out; serializeJson(doc, out);
    return out;
}
