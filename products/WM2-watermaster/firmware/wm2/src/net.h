#pragma once
// WiFi (farm network + own SoftAP), mDNS, NTP and MQTT to Girish's broker.
#include <Arduino.h>
#include <ArduinoJson.h>

namespace net {

void begin(bool doubleReset);
void loop();
bool wifiUp();
bool mqttUp();
bool apOn();
void startAp(uint32_t minutes);
String ip();
String joinStatus();  // after wifi_config: joining / joined / failed ("" = none)
String lastError();   // why the last join failed, in words ("" = none)
int rssi();
int apClients();
// Publish under <base>/<sub>. Returns false if MQTT is not connected.
bool publish(const char* sub, const String& payload, bool retain = false);
void reconfigure();          // WiFi/MQTT settings changed
// WiFi scan without blocking: results are cached and also pushed to app/page
// clients as {"type":"wifi_scan"}. Scanning makes the radio leave the SoftAP
// channel for ~2 s, so a phone on the SoftAP may see a short dropout.
void scanRequest();
bool scanBusy();
uint32_t scanAgeS();             // seconds since the cached list was made (0 = none)
void scanCached(JsonArray out);
void forgetWifi();               // clear farm WiFi, disconnect, SoftAP on

}  // namespace net
