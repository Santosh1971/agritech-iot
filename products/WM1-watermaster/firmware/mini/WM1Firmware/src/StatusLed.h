#pragma once
#include <Arduino.h>

// Mirrors WPC's non-blocking LED state machine (see
// products/WPC-pumpcontroller/firmware/master_node/src/main.cpp,
// updateWifiLed()) — GPIO2 is this board's onboard dev-kit LED,
// reserved for WiFi/power status on Mini's pinout and unused by
// anything else. update() must be called every loop() iteration; it
// never blocks, just checks millis() and toggles the pin when due.
//
// Three states:
//   AP failed to start        -> slow steady blink   (150ms/150ms)
//   AP up, no app connected   -> slow double-blink + long pause
//   AP up, app (WS) connected -> fast continuous blink (50ms/50ms)
class StatusLed {
public:
  static constexpr uint8_t PIN = 2;

  void begin() {
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);
  }

  void update(bool apOk, bool clientConnected) {
    static const uint32_t ERROR_DURATIONS[]     = {150, 150};
    static const uint32_t CONNECTED_DURATIONS[] = {50, 50};
    static const uint32_t IDLE_DURATIONS[]      = {80, 80, 80, 800};

    const uint32_t* durations;
    uint8_t phaseCount;
    if (!apOk)                { durations = ERROR_DURATIONS;     phaseCount = 2; }
    else if (clientConnected) { durations = CONNECTED_DURATIONS; phaseCount = 2; }
    else                      { durations = IDLE_DURATIONS;      phaseCount = 4; }

    uint32_t now = millis();
    if (now - _phaseStart >= durations[_phase]) {
      _phaseStart = now;
      _phase = (_phase + 1) % phaseCount;
      // Every pattern above alternates on/off starting ON — phase 0 and
      // 2 are "on", 1 and 3 are "off" (phase 3, the idle pattern's long
      // pause, is simply an extra-long "off").
      digitalWrite(PIN, (_phase % 2 == 0) ? HIGH : LOW);
    }
  }

private:
  uint32_t _phaseStart = 0;
  uint8_t _phase = 0;
};
