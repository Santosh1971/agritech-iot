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
    // 0.4.8: this class runs its own retry schedule. The core's
    // auto-reconnect kept the driver connecting in the background after
    // our attempt "timed out", and the driver rejects every scan while it
    // is connecting (bench log 2026-11-24: 5x "Driver rejected scan start").
    WiFi.setAutoReconnect(false);
#if defined(BOARD_WM_MINI_C3)
    // Super Mini boards have a poor antenna match: at full power the signal
    // is distorted and often not received. Capped as on the FG1 Greenhouse
    // TH C3 build (a common fix for these boards).
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif

    _apOk = WiFi.softAP(computeDeviceId().c_str(), SOFTAP_PASSWORD);
    Serial.printf("[WiFi] SoftAP started: %s\n", computeDeviceId().c_str());

    if (!_forcedLocal && _savedSsid.length()) {
      _startStaConnect();
    } else {
      _state = WifiState::SOFTAP_ACTIVE;
      if (_forcedLocal) Serial.println("[WiFi] force_local_mode active — not attempting STA");
    }
    _lastStaConnectedMillis = millis();
  }

  void loop() {
    uint32_t now = millis();

    if (_state == WifiState::STA_CONNECTING) {
      if (WiFi.status() == WL_CONNECTED) {
        _state = WifiState::STA_CONNECTED;
        _lastStaConnectedMillis = now;
        Serial.printf("[WiFi] STA connected, IP=%s\n", WiFi.localIP().toString().c_str());
      } else if (now - _staAttemptStart > STA_CONNECT_TIMEOUT_MS) {
        Serial.println("[WiFi] STA connect timed out — staying on SoftAP, will retry in background");
        WiFi.disconnect();   // actually stop the driver's attempt (0.4.8)
        _state = WifiState::SOFTAP_ACTIVE;
        _lastStaRetry = now;
      }
    } else if (_state == WifiState::STA_CONNECTED) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] STA link dropped — falling back to SoftAP-only, will retry");
        WiFi.disconnect();   // 0.4.8: no driver-side reconnect running behind our back
        _state = WifiState::SOFTAP_ACTIVE;
        _lastStaRetry = now;
      } else {
        _lastStaConnectedMillis = now;
      }
    } else {  // SOFTAP_ACTIVE
      if (_scanHoldUntil && (int32_t)(now - _scanHoldUntil) >= 0) releaseScanHold();
      if (!_scanHoldUntil && !_forcedLocal && _savedSsid.length() && (now - _lastStaRetry > STA_RETRY_INTERVAL_MS)) {
        _startStaConnect();
      }
    }

    // Reported live: the device can get stuck failing to reconnect
    // indefinitely after a WiFi drop, recoverable only by a manual
    // power cycle — a known category of ESP32 WiFi-driver behavior
    // where the radio gets into a state that repeated WiFi.begin()
    // calls alone can't clear, but a full reset can. Rather than chase
    // the exact internal driver state (which may vary), this is a
    // blunt but reliable backstop: if STA is SUPPOSED to be maintained
    // (not forced-local, real credentials saved) and hasn't actually
    // been connected for this long, restart outright — a clean boot
    // re-initializes the WiFi stack from scratch instead of retrying
    // WiFi.begin() against whatever bad state it's stuck in. Gated so
    // it never fires for a deliberate SoftAP-only install (no saved
    // SSID, or force_local_mode) — those are never "supposed" to be
    // STA-connected in the first place.
    // Force Local Mode (0.4.5): drop the home WiFi a moment after the
    // command, once MqttClientWrapper has sent its "offline" message.
    if (_dropStaAt && (int32_t)(now - _dropStaAt) >= 0) {
      _dropStaAt = 0;
      WiFi.disconnect();   // STA only; WIFI_MODE_APSTA keeps the SoftAP up
      Serial.println("[WiFi] Local mode: left home WiFi, SoftAP only");
    }

    // Never restart under someone who is using the board over its own
    // WiFi (0.4.5): a phone on the SoftAP doing Local Setup was cut off.
    if (WiFi.softAPgetStationNum() > 0) _lastStaConnectedMillis = now;

    if (!_forcedLocal && _savedSsid.length() && (now - _lastStaConnectedMillis > STA_STUCK_REBOOT_MS)) {
      Serial.println("[WiFi] STA hasn't reconnected in 5 minutes — restarting to clear a possibly stuck WiFi state");
      delay(100);
      ESP.restart();
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
    // Bug fix (0.4.5, tester log 2026-11-22 13:45:38): the 5-minute "stuck"
    // restart counted from boot, so new credentials sent after 5 minutes
    // without WiFi rebooted the board the same instant. Give the new
    // network a fresh 5 minutes.
    _lastStaConnectedMillis = millis();
    // Bug fix (0.4.6, tester report 2026-11-24: "select office WiFi, Connect,
    // device doesn't respond"): with Force Local Mode on (saved in NVS, so
    // it survives reboots) new credentials were stored and then ignored.
    // Picking a network and pressing Connect is an explicit request to go
    // online, so it ends Force Local Mode. Up to 0.4.4 this was hidden
    // because Force Local Mode never actually left the WiFi.
    if (_forcedLocal) {
      _forcedLocal = false;
      _prefs.putBool("forced_local", false);
      _dropStaAt = 0;
      Serial.println("[WiFi] force_local_mode = false (new WiFi details)");
    }
    _startStaConnect();
  }

  void setForcedLocal(bool forced) {
    _forcedLocal = forced;
    _prefs.putBool("forced_local", forced);
    Serial.printf("[WiFi] force_local_mode = %s\n", forced ? "true" : "false");
    _lastStaConnectedMillis = millis();
    if (forced) {
      // Bug fix (0.4.5, tester report): this only saved the flag and the
      // board stayed on the home WiFi (and online in Cloud mode). Now it
      // really leaves it: isStaConnected() goes false at once, so MQTT
      // publishes "offline" and disconnects on the next loop, then the STA
      // link is dropped (see loop()). The SoftAP stays up for the phone.
      if (_state != WifiState::SOFTAP_ACTIVE) _dropStaAt = millis() + 500;
      _state = WifiState::SOFTAP_ACTIVE;
    } else if (_savedSsid.length()) {
      _startStaConnect();
    }
  }

  // WiFi scan (0.4.7, tester: "driver rejected the scan start"): the ESP32
  // refuses to start a scan while the STA is in the middle of a connect
  // attempt, and with saved but unreachable credentials the board retries
  // one every 30 s (15 s each). Abort any attempt in flight and hold off
  // new ones until the scan is done (or maxMs passes).
  void holdStaForScan(uint32_t maxMs) {
    uint32_t now = millis();
    // 0.4.8: stop the driver whenever STA is not actually connected, not
    // only when our own state says "connecting" -- the driver may still be
    // trying in the background.
    if (_state != WifiState::STA_CONNECTED) {
      WiFi.disconnect();
      _state = WifiState::SOFTAP_ACTIVE;
      Serial.println("[WiFi] Stopped STA connecting for WiFi scan");
    }
    _scanHoldUntil = now + maxMs;
    _lastStaConnectedMillis = now;
  }
  void releaseScanHold() {
    if (!_scanHoldUntil) return;
    _scanHoldUntil = 0;
    _lastStaRetry = millis() - STA_RETRY_INTERVAL_MS + 2000;  // retry ~2 s after the scan
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
  static constexpr uint32_t STA_STUCK_REBOOT_MS = 5UL * 60UL * 1000UL;

  Preferences _prefs;
  WifiState _state = WifiState::SOFTAP_ACTIVE;
  bool _forcedLocal = false;
  bool _apOk = true;
  String _savedSsid, _savedPass;
  uint32_t _staAttemptStart = 0;
  uint32_t _lastStaRetry = 0;
  uint32_t _lastStaConnectedMillis = 0;
  uint32_t _dropStaAt = 0;
  uint32_t _scanHoldUntil = 0;
};
