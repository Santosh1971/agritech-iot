#pragma once
#include <Arduino.h>

// RelayController is the hardware abstraction for however many relay
// channels physically exist. Today's prototype board has exactly ONE
// relay wired directly to a GPIO. The final Water Manager-Mini board
// has SIX, driven through a 74HC595 + ULN2003 shift-register chain.
//
// Everything above this layer (IrrigationController, Scheduler) talks
// only in terms of channel indices 0..N-1 and never knows or cares how
// a channel is actually driven. Swapping from today's direct-GPIO
// implementation to the real shift-register implementation later is a
// one-file change here, not a rewrite of the control logic.

class RelayController {
public:
  virtual ~RelayController() = default;
  virtual void begin() = 0;
  virtual void setChannel(uint8_t index, bool on) = 0;
  virtual bool getChannel(uint8_t index) const = 0;
  virtual uint8_t channelCount() const = 0;

  // Convenience: turn every channel off. Used for the fail-safe path
  // (power loss, IN1 LOW, boot) where "all relays off" must be
  // reachable in one call, not N separate ones.
  void allOff() {
    for (uint8_t i = 0; i < channelCount(); i++) setChannel(i, false);
  }
};

// --- Today's hardware: a single relay on a fixed GPIO -----------------
// Config note: on the real board, channel 0 = pump (RL1), channel 1 =
// dosing (RL2), channels 2-5 = the four irrigation valves (RL3-RL6).
// With only one physical relay available right now, we still expose
// a 6-channel logical interface — channel 2 (the first valve) drives
// the real relay, every other channel just logs to Serial as if it
// switched, so the IrrigationController logic above can be exercised
// and observed exactly as it will run against the real board later.

class DirectGpioRelayController : public RelayController {
public:
  static constexpr uint8_t LOGICAL_CHANNEL_COUNT = 6;
  static constexpr uint8_t PHYSICAL_CHANNEL = 2;  // first valve = the real relay
  static constexpr uint8_t PHYSICAL_GPIO = 26;    // wire to whatever pin your relay board uses

  void begin() override {
    pinMode(PHYSICAL_GPIO, OUTPUT);
    digitalWrite(PHYSICAL_GPIO, LOW);
    for (uint8_t i = 0; i < LOGICAL_CHANNEL_COUNT; i++) _state[i] = false;
  }

  void setChannel(uint8_t index, bool on) override {
    if (index >= LOGICAL_CHANNEL_COUNT) return;
    _state[index] = on;
    if (index == PHYSICAL_CHANNEL) {
      digitalWrite(PHYSICAL_GPIO, on ? HIGH : LOW);
    } else {
      Serial.printf("[RelayController] (simulated) channel %u -> %s\n",
                    index, on ? "ON" : "OFF");
    }
  }

  bool getChannel(uint8_t index) const override {
    return (index < LOGICAL_CHANNEL_COUNT) ? _state[index] : false;
  }

  uint8_t channelCount() const override { return LOGICAL_CHANNEL_COUNT; }

private:
  bool _state[LOGICAL_CHANNEL_COUNT] = {false};
};

// --- Future hardware: 74HC595 + ULN2003, 6 real relays -----------------
// Skeleton only — fill in shiftOut()/latch pin wiring once the real
// board arrives. Kept here now so the class name/shape is settled and
// call sites elsewhere don't need to change later, only this body.

class ShiftRegisterRelayController : public RelayController {
public:
  static constexpr uint8_t CHANNEL_COUNT = 6;

  ShiftRegisterRelayController(uint8_t dataPin, uint8_t clockPin, uint8_t latchPin)
    : _dataPin(dataPin), _clockPin(clockPin), _latchPin(latchPin) {}

  void begin() override {
    pinMode(_dataPin, OUTPUT);
    pinMode(_clockPin, OUTPUT);
    pinMode(_latchPin, OUTPUT);
    _pushState();
  }

  void setChannel(uint8_t index, bool on) override {
    if (index >= CHANNEL_COUNT) return;
    if (on) _state |= (1 << index); else _state &= ~(1 << index);
    _pushState();
  }

  bool getChannel(uint8_t index) const override {
    return (index < CHANNEL_COUNT) ? (_state & (1 << index)) != 0 : false;
  }

  uint8_t channelCount() const override { return CHANNEL_COUNT; }

private:
  void _pushState() {
    digitalWrite(_latchPin, LOW);
    shiftOut(_dataPin, _clockPin, MSBFIRST, _state);
    digitalWrite(_latchPin, HIGH);
  }

  uint8_t _dataPin, _clockPin, _latchPin;
  uint8_t _state = 0;
};
