#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include "Config.h"

// Computes "WM1_XXXXXXXX" where XXXXXXXX is the last 4 bytes of the
// chip's factory MAC, uppercase hex — 8 characters, chosen so two
// nearby units are very unlikely to collide on suffix (4 chars/2 bytes
// was too short in practice — plausible collisions across a dealer's
// stock of boards). Cached after first call.
inline String computeDeviceId() {
  static String cached;
  if (cached.length()) return cached;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[9];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);

  cached = String(DEVICE_MODEL_PREFIX) + "_" + suffix;
  return cached;
}
