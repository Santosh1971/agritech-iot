#pragma once
#include <Arduino.h>

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
// IN2 (GPIO32) is repurposed here specifically as a water-level float
// switch input — the spec left IN2/IN3 as "generic, undefined" pending
// a real use; this is that use. Assumed wiring: a normally-closed float
// switch to GND (matches IN1's existing pulled-up pattern) — HIGH =
// level OK, LOW = low-level alarm. Confirm polarity once real hardware
// exists; until then this is a documented assumption, not a measurement.
class Sensors {
public:
  static constexpr uint8_t PIN_FLOW = 36;
  static constexpr uint8_t PIN_PRESS1 = 39;
  static constexpr uint8_t PIN_PRESS2 = 34;
  static constexpr uint8_t PIN_BATT = 35;
  static constexpr uint8_t PIN_WATER_LEVEL = 32;  // IN2, repurposed

  // Incremented by main.cpp's ISR (kept as a free function there, same
  // convention as the rest of this firmware's interrupt handling) —
  // public so the ISR can reach it without any static-instance-pointer
  // machinery in a header.
  volatile uint32_t pulseCount = 0;

  void begin() {
    pinMode(PIN_FLOW, INPUT);
    pinMode(PIN_WATER_LEVEL, INPUT_PULLUP);
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
  bool waterLevelOk() const { return digitalRead(PIN_WATER_LEVEL) == HIGH; }
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
};
