#include "WiFiScanner.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>

void WiFiScanner::startScan() {
    Serial.println("[WiFi] Scanning networks (async)...");
    _lastFound = WIFI_SCAN_RUNNING;
    _retries = 0;
    _zeroRetried = false;
    _restartAt = 0;
    _begin();
}

void WiFiScanner::_begin() {
    _scanStartMillis = millis();
    // Default per-channel dwell (~120ms) is often too short to catch
    // other APs' beacons while concurrently running our own SoftAP
    // (confirmed: consistently 0 networks). 500ms made each scan run
    // right into the core's 10s timeout (see WiFiScanner.h); 300ms
    // keeps the longer dwell with the scan finishing in ~5s.
    // Params: async, no hidden, active probe, ms per channel.
    int16_t r = WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHAN);
    _startRejected = (r == WIFI_SCAN_FAILED);
    if (_startRejected) Serial.println("[WiFi] Driver rejected scan start");
}

bool WiFiScanner::checkComplete() {
    if (_restartAt) {
        if ((int32_t)(millis() - _restartAt) < 0) return false;
        _restartAt = 0;
        WiFi.scanDelete();  // drop the aborted scan's SCAN_DONE
        _begin();
        return false;
    }

    int result = WiFi.scanComplete();
    uint32_t elapsed = millis() - _scanStartMillis;

    if (result >= 0) {
        // A genuine 0 is rare (the Mac next to the bench alone sees a
        // dozen APs) — one quiet re-scan is cheap insurance.
        if (result == 0 && !_zeroRetried) {
            _zeroRetried = true;
            Serial.println("[WiFi] Scan found 0 networks — rescanning once");
            WiFi.scanDelete();
            _begin();
            return false;
        }
        _lastFound = result;
        return true;
    }

    // RUNNING, or FAILED from the core's own timeout while the driver is
    // actually still scanning — keep waiting either way. Exception: the
    // driver refused to start at all, so there's nothing to wait for.
    uint32_t limit = _startRejected ? REJECTED_RETRY_MS : MAX_SCAN_MS;
    if (elapsed < limit) return false;

    if (!_startRejected) {
        Serial.println("[WiFi] Scan did not finish in time — stopping it");
        esp_wifi_scan_stop();
    }
    if (_retries < MAX_RETRIES) {
        _retries++;
        Serial.printf("[WiFi] Retrying scan (%d/%d)...\n", _retries, MAX_RETRIES);
        _restartAt = millis() + RESTART_DELAY_MS;
        return false;
    }
    WiFi.scanDelete();
    _lastFound = WIFI_SCAN_FAILED;
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
