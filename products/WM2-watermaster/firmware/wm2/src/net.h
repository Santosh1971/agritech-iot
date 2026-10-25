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
int rssi();
int apClients();
// Publish under <base>/<sub>. Returns false if MQTT is not connected.
bool publish(const char* sub, const String& payload, bool retain = false);
void reconfigure();          // WiFi/MQTT settings changed
void scanStart();            // async WiFi scan; results via scanResult()
bool scanResult(JsonArray out);  // false while still scanning

}  // namespace net
