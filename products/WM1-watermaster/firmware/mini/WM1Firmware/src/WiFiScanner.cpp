#include "WiFiScanner.h"
#include <ArduinoJson.h>

void WiFiScanner::startScan() {
    Serial.println("[WiFi] Scanning networks (async)...");
    _lastFound = WIFI_SCAN_RUNNING;
    _retries = 0;
    _scanStartMillis = millis();
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
        // Confirmed live: with STA already connected to a router, the
        // driver can sit in WIFI_SCAN_RUNNING forever and never report
        // FAILED either — this hard ceiling is what actually bounds
        // that case, since the retry logic below only ever triggers on
        // an explicit FAILED result.
        if (millis() - _scanStartMillis > MAX_SCAN_MS) {
            Serial.println("[WiFi] Scan stuck RUNNING past the time limit — aborting");
            WiFi.scanDelete();
            _lastFound = WIFI_SCAN_FAILED;
            return true;
        }
        _lastFound = result;
        return false;
    }

    if (result == WIFI_SCAN_FAILED && _retries < MAX_RETRIES) {
        _retries++;
        Serial.printf("[WiFi] Scan failed — retrying (%d/%d)...\n", _retries, MAX_RETRIES);
        // Bug fix: this used to retry with WiFi.scanNetworks(true) —
        // bare defaults, NOT the tuned params startScan() uses. The
        // whole reason for those params (500ms/channel dwell) is that
        // the default is too short under concurrent AP+STA; retrying
        // with the untuned default undermined the retry's own purpose,
        // and confirmed live: every retry failed the same way the
        // original attempt did.
        WiFi.scanNetworks(true, false, false, 500);
        _lastFound = WIFI_SCAN_RUNNING;
        _scanStartMillis = millis();  // ceiling above applies per-attempt, not cumulatively
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
    if (found < 0) {
      Serial.printf("[WiFi] Scan ultimately failed (code %d) after exhausting retries\n", found);
    } else {
      Serial.printf("[WiFi] Scan found %d networks\n", found);
    }
    String out; serializeJson(doc, out);
    return out;
}
