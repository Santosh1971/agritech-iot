#pragma once
#include <Arduino.h>

// RelayController is the hardware abstraction for however many relay
// channels physically exist.
//
// IMPORTANT (corrected from the first version): the real board has
// TWO daisy-chained 74HC595s on the same RLY_DATA/RLY_CLK/RLY_LATCH
// lines — U2 drives the 6 relays (RELAY1-6_MCU on QA-QF), and its
// QH' output feeds directly into U9's SER input, which drives 7
// status LEDs (Flow_LED, PR1_LED, PR2_LED, IN1_LED, IN2_LED, IN3_LED,
// LOBATT_LED on QA-QG; QH' chains onward to the future extension
// board). That's 16 bits to shift out every time, not 8 — shifting
// only the relay byte (as the first version of this file did) leaves
// U9 receiving whatever bits happen to fall out of U2's *previous*
// contents on those same clock pulses, which is exactly what was
// making the LEDs flicker on every relay toggle.
//
// Shift-register cascade rule: whichever byte you shift out FIRST
// ends up furthest down the chain after the second shiftOut. Since
// U9 is downstream of U2, the LED byte must be shifted first, then
// the relay byte — the relay byte lands in U2, and shifting it pushes
// the already-loaded LED byte the rest of the way into U9. Latch
// once, after both bytes are shifted, so both chips update together
// with no visible partial state.

class RelayController {
public:
  virtual ~RelayController() = default;
  virtual void begin() = 0;
  virtual void setChannel(uint8_t index, bool on) = 0;
  virtual bool getChannel(uint8_t index) const = 0;
  virtual uint8_t channelCount() const = 0;
  void allOff() {
    for (uint8_t i = 0; i < channelCount(); i++) setChannel(i, false);
  }
};

// --- Real hardware: 2x 74HC595 daisy-chained (U2 relays, U9 LEDs) -----
//
// LED channel bits are exposed too (LED_* constants below) purely so
// firmware CAN drive them once flow/pressure/IN1 are actually defined
// — but per your instruction, every LED bit defaults to 0 (off) and
// nothing in this class turns one on unless explicitly told to. The
// relay bits are the only thing normal scheduler/irrigation operation
// touches.

class ShiftRegisterRelayController : public RelayController {
public:
  static constexpr uint8_t RELAY_CHANNEL_COUNT = 6;  // RL1-RL6

  // LED bit positions within the LED byte (U9, QA..QG) — for later use.
  static constexpr uint8_t LED_FLOW    = 0;
  static constexpr uint8_t LED_PR1     = 1;
  static constexpr uint8_t LED_PR2     = 2;
  static constexpr uint8_t LED_IN1     = 3;
  static constexpr uint8_t LED_IN2     = 4;
  static constexpr uint8_t LED_IN3     = 5;
  static constexpr uint8_t LED_LOBATT  = 6;
  // bit 7 (QH) unused on U9

  ShiftRegisterRelayController(uint8_t dataPin, uint8_t clockPin, uint8_t latchPin)
    : _dataPin(dataPin), _clockPin(clockPin), _latchPin(latchPin) {}

  void begin() override {
    pinMode(_dataPin, OUTPUT);
    pinMode(_clockPin, OUTPUT);
    pinMode(_latchPin, OUTPUT);
    _relayByte = 0;
    _ledByte = 0;      // all indicator LEDs off at boot, per instruction
    _pushState();
  }

  void setChannel(uint8_t index, bool on) override {
    if (index >= RELAY_CHANNEL_COUNT) return;
    if (on) _relayByte |= (1 << index); else _relayByte &= ~(1 << index);
    _pushState();
  }

  bool getChannel(uint8_t index) const override {
    return (index < RELAY_CHANNEL_COUNT) ? (_relayByte & (1 << index)) != 0 : false;
  }

  uint8_t channelCount() const override { return RELAY_CHANNEL_COUNT; }

  // Explicit, separate from the relay path above — nothing calls this
  // during normal scheduler/irrigation operation. Available for when
  // flow/pressure/IN1 get real definitions and firmware needs to
  // reflect their state on the indicator LEDs.
  void setLed(uint8_t ledBit, bool on) {
    if (ledBit > 7) return;
    if (on) _ledByte |= (1 << ledBit); else _ledByte &= ~(1 << ledBit);
    _pushState();
  }

  void allLedsOff() { _ledByte = 0; _pushState(); }

private:
  void _pushState() {
    digitalWrite(_latchPin, LOW);
    // LED byte first (ends up in U9, the downstream chip), THEN the
    // relay byte (lands in U2, pushing the LED byte the rest of the
    // way into U9). Reversing this order is the bug this class fixes.
    shiftOut(_dataPin, _clockPin, MSBFIRST, _ledByte);
    shiftOut(_dataPin, _clockPin, MSBFIRST, _relayByte);
    digitalWrite(_latchPin, HIGH);
  }

  uint8_t _dataPin, _clockPin, _latchPin;
  uint8_t _relayByte = 0;
  uint8_t _ledByte = 0;
};

// --- Today's bring-up hardware: single relay on a fixed GPIO ----------
// Kept for reference / earlier bench testing — the real board (above)
// is now confirmed working, so ShiftRegisterRelayController is what
// production code should use going forward.
class DirectGpioRelayController : public RelayController {
public:
  static constexpr uint8_t LOGICAL_CHANNEL_COUNT = 6;
  static constexpr uint8_t PHYSICAL_CHANNEL = 2;
  static constexpr uint8_t PHYSICAL_GPIO = 26;

  void begin() override {
    pinMode(PHYSICAL_GPIO, OUTPUT);
    digitalWrite(PHYSICAL_GPIO, LOW);
    for (uint8_t i = 0; i < LOGICAL_CHANNEL_COUNT; i++) _state[i] = false;
  }
  void setChannel(uint8_t index, bool on) override {
    if (index >= LOGICAL_CHANNEL_COUNT) return;
    _state[index] = on;
    if (index == PHYSICAL_CHANNEL) digitalWrite(PHYSICAL_GPIO, on ? HIGH : LOW);
  }
  bool getChannel(uint8_t index) const override {
    return (index < LOGICAL_CHANNEL_COUNT) ? _state[index] : false;
  }
  uint8_t channelCount() const override { return LOGICAL_CHANNEL_COUNT; }
private:
  bool _state[LOGICAL_CHANNEL_COUNT] = {false};
};
