#include "WiFiScanner.h"
#include <ArduinoJson.h>

String WiFiScanner::scanAsJson() {
    Serial.println("[WiFi] Scanning networks...");
    // Negative results (WIFI_SCAN_FAILED=-1, WIFI_SCAN_RUNNING=-2) usually
    // mean the radio was transiently busy — most likely the background
    // WiFi retry (see main.cpp's beginBackgroundRetry) mid-attempt, since
    // WiFi.begin() itself does an internal scan-for-AP as part of
    // connecting. Retry a few times with a short gap rather than
    // reporting "0 networks found" for what's actually a busy radio.
    int found = WiFi.scanNetworks();
    for (int attempt = 0; found < 0 && attempt < 3; attempt++) {
        Serial.printf("[WiFi] Scan busy (code %d) — retrying...\n", found);
        delay(500);
        found = WiFi.scanNetworks();
    }

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
