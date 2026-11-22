#pragma once
#include <Arduino.h>
#include "Board.h"

// Mirrors WPC's non-blocking LED state machine (see
// products/WPC-pumpcontroller/firmware/master_node/src/main.cpp,
// updateWifiLed()) — GPIO2 is this board's onboard dev-kit LED,
// reserved for WiFi/power status on Mini's pinout and unused by
// anything else. update() must be called every loop() iteration; it
// never blocks, just checks millis() and toggles the pin when due.
//
// Same four patterns as WPC's master_node updateWifiLed() and FG1's
// LEDManager (tester request 2026-11-22: "should blink slowly like WPC,
// FG1" -- this used to show a quick double blink whenever no app was
// connected, which only means "all OK" on the other products):
//   AP failed to start           -> 150ms / 150ms
//   on home WiFi (internet path) -> double blink: 100/100/100 on-off, 1000 pause
//   phone joined / app connected -> fast 50ms / 50ms
//   SoftAP only, nobody joined   -> slow 1000ms / 1000ms
class StatusLed {
public:
  static constexpr uint8_t PIN = PIN_WIFI_LED;   // GPIO2 on WM1-Mini, IO8 (active LOW) on WM-Mini C3

  static void write(bool on) { digitalWrite(PIN, (on != WIFI_LED_ACTIVE_LOW) ? HIGH : LOW); }

  void begin() {
    pinMode(PIN, OUTPUT);
    write(false);
  }

  void update(bool apOk, bool staConnected, bool clientConnected) {
    static const uint32_t ERROR_DURATIONS[]     = {150, 150};
    static const uint32_t INTERNET_DURATIONS[]  = {100, 100, 100, 1000};
    static const uint32_t CONNECTED_DURATIONS[] = {50, 50};
    static const uint32_t IDLE_DURATIONS[]      = {1000, 1000};

    const uint32_t* durations;
    uint8_t phaseCount;
    uint8_t state;
    if (!apOk)                { durations = ERROR_DURATIONS;     phaseCount = 2; state = 0; }
    else if (staConnected)    { durations = INTERNET_DURATIONS;  phaseCount = 4; state = 1; }
    else if (clientConnected) { durations = CONNECTED_DURATIONS; phaseCount = 2; state = 2; }
    else                      { durations = IDLE_DURATIONS;      phaseCount = 2; state = 3; }

    uint32_t now = millis();
    if (state != _state) {   // restart the new pattern from its first "on"
      _state = state; _phase = 0; _phaseStart = now; write(true);
      return;
    }
    if (now - _phaseStart >= durations[_phase]) {
      _phaseStart = now;
      _phase = (_phase + 1) % phaseCount;
      // Every pattern starts ON and alternates: even phases on, odd off.
      write(_phase % 2 == 0);
    }
  }

private:
  uint32_t _phaseStart = 0;
  uint8_t _phase = 0;
  uint8_t _state = 0xFF;
};
