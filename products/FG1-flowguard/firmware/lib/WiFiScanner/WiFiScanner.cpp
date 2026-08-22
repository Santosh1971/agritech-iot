#include "WiFiScanner.h"
#include <ArduinoJson.h>

void WiFiScanner::startScan() {
    Serial.println("[WiFi] Scanning networks (async)...");
    _lastFound = WIFI_SCAN_RUNNING;
    _retries = 0;
    // Default per-channel dwell (~120ms) is often too short to catch
    // other APs' beacon frames while concurrently running our own
    // SoftAP — confirmed on real hardware (scan technically succeeded
    // but consistently found 0 networks). Explicit params: async=true,
    // show_hidden=false, passive=false (active probe — faster and more
    // reliable than passive for this case), max_ms_per_chan=500 (up
    // from the ~120ms default).
    WiFi.scanNetworks(true, false, false, 500);
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
        WiFi.scanNetworks(true);
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
    if (found > 0) {
        for (int i = 0; i < min(found, 15); i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i);
            o["rssi"] = WiFi.RSSI(i);
            o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        }
    }
    WiFi.scanDelete();
    Serial.printf("[WiFi] Scan found %d networks\n", found);
    String out; serializeJson(doc, out);
    return out;
}
