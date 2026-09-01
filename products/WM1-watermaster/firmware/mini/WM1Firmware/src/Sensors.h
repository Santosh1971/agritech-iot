#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Pin map per spec §2.2, confirmed bench-tested via HardwareBringupTest
// on the real PCB (flow pulse counting, pressure/battery ADC all read
// correctly there). What's NEW here is turning those raw readings into
// engineering units and exposing them over the app's status JSON —
// there is no real 4-20mA pressure sensor, flow meter, or level switch
// wired up yet (per project notes), so every scale factor below is an
// explicitly-flagged PLACEHOLDER, not a measured calibration. The
// plumbing (ADC reads, pulse counting, digital read) is real and
// verified; the numbers it produces are not accurate until someone
// calibrates against the real sensors once Kamta's site has them.
//
// IN2 (GPIO32) and IN3 (GPIO33) are repurposed here as the two
// source-level float switches (L1/L2) the app's dry-run protection
// feature needs — the spec left both as "generic, undefined" pending a
// real use; this is that use. Assumed wiring: normally-open float
// switches to GND (same pulled-up convention as IN1, and matching
// WPC's real float-switch polarity) — LOW = water present at that
// level, HIGH = water has receded past it. A borewell-fed farmer has
// nothing wired to these pins at all, which is why this is gated by
// _levelEnabled (persisted, defaults OFF): with it off, these pins are
// never read and never affect scheduling, so a farmer without the
// sensor never sees an unconnected pin's floating HIGH read as "dry"
// and get irrigation wrongly blocked.
class Sensors {
public:
  static constexpr uint8_t PIN_FLOW = 36;
  static constexpr uint8_t PIN_PRESS1 = 39;
  static constexpr uint8_t PIN_PRESS2 = 34;
  static constexpr uint8_t PIN_BATT = 35;
  static constexpr uint8_t PIN_WATER_L1 = 32;  // IN2, repurposed
  static constexpr uint8_t PIN_WATER_L2 = 33;  // IN3, repurposed

  // Incremented by main.cpp's ISR (kept as a free function there, same
  // convention as the rest of this firmware's interrupt handling) —
  // public so the ISR can reach it without any static-instance-pointer
  // machinery in a header.
  volatile uint32_t pulseCount = 0;

  void begin() {
    pinMode(PIN_FLOW, INPUT);
    pinMode(PIN_WATER_L1, INPUT_PULLUP);
    pinMode(PIN_WATER_L2, INPUT_PULLUP);
    _prefs.begin("wm1", false);
    _levelEnabled = _prefs.getBool("lvl_en", false);
  }

  bool waterLevelEnabled() const { return _levelEnabled; }
  void setWaterLevelEnabled(bool enabled) {
    _levelEnabled = enabled;
    _prefs.putBool("lvl_en", enabled);
  }

  // Call once per loop() — cheap; only actually recomputes once a
  // second, converting the last second's pulses into a rate and adding
  // to the running total.
  void update() {
    uint32_t now = millis();
    if (now - _lastRateCalc < 1000) return;
    _lastRateCalc = now;

    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float liters = pulses / PULSES_PER_LITER;
    _flowRateLpm = liters * 60.0f;
    _flowTotalLiters += liters;
  }

  float pressure1Bar() const { return _adcToBar(analogRead(PIN_PRESS1)); }
  float pressure2Bar() const { return _adcToBar(analogRead(PIN_PRESS2)); }
  float flowRateLpm() const { return _flowRateLpm; }
  float flowTotalLiters() const { return _flowTotalLiters; }

  // Raw per-switch reads — LOW = water present at that level. Only
  // meaningful (and only read) when waterLevelEnabled(); with no
  // simulate override active, main.cpp is the one place these are
  // actually read from the real pins.
  bool waterL1Ok() const { return _simulated ? _simL1Ok : digitalRead(PIN_WATER_L1) == LOW; }
  bool waterL2Ok() const { return _simulated ? _simL2Ok : digitalRead(PIN_WATER_L2) == LOW; }

  // Combined dry-run check: water is considered OK unless BOTH switches
  // read absent (water has receded below L1, the lower of the two) —
  // matches the app's described intent exactly. Always true when the
  // feature isn't enabled for this installation, so nothing downstream
  // needs its own enabled-check.
  bool waterLevelOk() const { return !_levelEnabled || (waterL1Ok() || waterL2Ok()); }

  // Bench-test stand-in for the real float switches, same pattern as
  // simulate_power_loss/restore — lets the dry-run pause be exercised
  // and demoed before any real L1/L2 hardware is wired up.
  void simulateLevels(bool l1Ok, bool l2Ok) {
    _simulated = true;
    _simL1Ok = l1Ok;
    _simL2Ok = l2Ok;
  }
  void clearSimulation() { _simulated = false; }

  float batteryVolts() const { return analogRead(PIN_BATT) * (3.3f / 4095.0f) * BATT_DIVIDER_RATIO; }

private:
  // PLACEHOLDER: assumes a 250-ohm shunt converting 4-20mA to 1-5V,
  // then maps 4-20mA linearly across an assumed 0-10 bar sensor range.
  // Replace both the shunt value and the bar range with the real
  // sensor's datasheet once one is wired.
  static float _adcToBar(int raw) {
    float volts = raw * (3.3f / 4095.0f);
    float mA = (volts / 250.0f) * 1000.0f;
    float bar = (mA - 4.0f) / 16.0f * 10.0f;
    return bar < 0 ? 0 : bar;
  }

  static constexpr float PULSES_PER_LITER = 5.5f;   // PLACEHOLDER K-factor — replace with real flow meter spec
  static constexpr float BATT_DIVIDER_RATIO = 2.0f; // PLACEHOLDER — replace with real resistor divider ratio

  float _flowRateLpm = 0;
  float _flowTotalLiters = 0;
  uint32_t _lastRateCalc = 0;

  Preferences _prefs;
  bool _levelEnabled = false;
  bool _simulated = false;
  bool _simL1Ok = true;
  bool _simL2Ok = true;
};
