#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Config.h"
#include "DeviceIdentity.h"

// Non-blocking hybrid WiFi manager, following the pattern documented
// for FG1 (updateConnMode() hysteresis, SoftAP fallback, force_local_mode
// persisted across reboots). This is a fresh implementation of that
// pattern, not copied from the real source (couldn't fetch it) — the
// STATE MACHINE SHAPE matches what's documented; exact timings/edge
// cases will need reconciling against the real FG1 firmware and, more
// importantly, real hardware testing (FG1's history shows several
// subtle bugs here — e.g. WiFi.setSleep(false) being required to avoid
// periodic disconnects, and localServer.begin() needing to run AFTER
// WiFi.mode() or it triggers a reboot loop — both applied below
// proactively since they're already known issues, not discovered here).

enum class WifiState : uint8_t { STA_CONNECTING, STA_CONNECTED, SOFTAP_ACTIVE };

class WiFiManager {
public:
  void begin() {
    _prefs.begin(NVS_NAMESPACE, false);
    _forcedLocal = _prefs.getBool("forced_local", false);
    _savedSsid = _prefs.getString("wifi_ssid", "");
    _savedPass = _prefs.getString("wifi_pass", "");

    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.setSleep(false);  // known FG1 fix — avoids periodic ASSOC_LEAVE disconnects

    _apOk = WiFi.softAP(computeDeviceId().c_str(), SOFTAP_PASSWORD);
    Serial.printf("[WiFi] SoftAP started: %s\n", computeDeviceId().c_str());

    if (!_forcedLocal && _savedSsid.length()) {
      _startStaConnect();
    } else {
      _state = WifiState::SOFTAP_ACTIVE;
      if (_forcedLocal) Serial.println("[WiFi] force_local_mode active — not attempting STA");
    }
  }

  void loop() {
    uint32_t now = millis();

    if (_state == WifiState::STA_CONNECTING) {
      if (WiFi.status() == WL_CONNECTED) {
        _state = WifiState::STA_CONNECTED;
        Serial.printf("[WiFi] STA connected, IP=%s\n", WiFi.localIP().toString().c_str());
      } else if (now - _staAttemptStart > STA_CONNECT_TIMEOUT_MS) {
        Serial.println("[WiFi] STA connect timed out — staying on SoftAP, will retry in background");
        _state = WifiState::SOFTAP_ACTIVE;
        _lastStaRetry = now;
      }
    } else if (_state == WifiState::STA_CONNECTED) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] STA link dropped — falling back to SoftAP-only, will retry");
        _state = WifiState::SOFTAP_ACTIVE;
        _lastStaRetry = now;
      }
    } else {  // SOFTAP_ACTIVE
      if (!_forcedLocal && _savedSsid.length() && (now - _lastStaRetry > STA_RETRY_INTERVAL_MS)) {
        _startStaConnect();
      }
    }
  }

  bool isStaConnected() const { return _state == WifiState::STA_CONNECTED; }
  WifiState state() const { return _state; }
  String deviceId() const { return computeDeviceId(); }

  void setCredentials(const String& ssid, const String& password) {
    _savedSsid = ssid;
    _savedPass = password;
    _prefs.putString("wifi_ssid", ssid);
    _prefs.putString("wifi_pass", password);
    Serial.printf("[WiFi] Credentials saved for SSID: %s\n", ssid.c_str());
    if (!_forcedLocal) _startStaConnect();
  }

  void setForcedLocal(bool forced) {
    _forcedLocal = forced;
    _prefs.putBool("forced_local", forced);
    Serial.printf("[WiFi] force_local_mode = %s\n", forced ? "true" : "false");
    if (!forced && _savedSsid.length()) _startStaConnect();
  }

  bool isForcedLocal() const { return _forcedLocal; }
  bool apOk() const { return _apOk; }

private:
  void _startStaConnect() {
    // Defensive: if a previous attempt is still in flight (e.g. new
    // credentials submitted again before the last attempt timed out),
    // calling WiFi.begin() straight over it is exactly what produces
    // ESP-IDF's "sta is connecting, return error" — confirmed live.
    // Disconnecting first abandons that attempt cleanly instead of
    // racing it.
    if (_state == WifiState::STA_CONNECTING) {
      WiFi.disconnect();
    }
    Serial.printf("[WiFi] Attempting STA connect to: %s\n", _savedSsid.c_str());
    WiFi.begin(_savedSsid.c_str(), _savedPass.c_str());
    _staAttemptStart = millis();
    _state = WifiState::STA_CONNECTING;
  }

  static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t STA_RETRY_INTERVAL_MS = 30000;

  Preferences _prefs;
  WifiState _state = WifiState::SOFTAP_ACTIVE;
  bool _forcedLocal = false;
  bool _apOk = true;
  String _savedSsid, _savedPass;
  uint32_t _staAttemptStart = 0;
  uint32_t _lastStaRetry = 0;
};
