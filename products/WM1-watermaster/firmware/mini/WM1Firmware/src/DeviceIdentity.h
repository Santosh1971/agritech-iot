#pragma once
#include <Arduino.h>
#include <esp_system.h>
#include "Config.h"

// Computes "WM1_XXXX" where XXXX is the last 2 bytes of the chip's
// factory MAC, uppercase hex — same pattern as FG1's SWC_001_XXXX.
// Cached after first call.
inline String computeDeviceId() {
  static String cached;
  if (cached.length()) return cached;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);

  cached = String(DEVICE_MODEL_PREFIX) + "_" + suffix;
  return cached;
}
