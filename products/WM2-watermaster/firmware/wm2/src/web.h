#pragma once
// Local web server: control page at /, JSON API, WebSocket /ws, CSV export.
// Reached over the farm WiFi (http://wm2-z1-n1.local or the node's IP) or
// the node's SoftAP (http://192.168.4.1). Same command JSON as MQTT.
#include <Arduino.h>

namespace web {
void begin();
void loop();
void broadcast(const String& json);  // to all WebSocket clients
int clients();
}  // namespace web
