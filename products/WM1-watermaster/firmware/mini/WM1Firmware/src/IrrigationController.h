#pragma once
#include "RelayController.h"

// Fixed channel roles, per the Water Manager-Mini spec (§2.3):
//   0 = pump (RL1)      — auto-driven, never commanded directly
//   1 = dosing (RL2)    — single fertigation channel, time-based only
//   2..5 = valves (RL3-RL6) — the 4 irrigation zones
//
// This class is where the rules from the spec that are easy to get
// subtly wrong live, in one place:
//   - pump is ON iff at least one valve is ON (never pump-only)
//   - dosing must never run with every valve closed — dosing into a
//     line with no flow just dumps concentrate with nowhere to go.
//     Enforced as a hard invariant here (not just a check on the
//     manual_set command path) so it holds no matter what turns a
//     valve off — manual toggle, a sequence ending, force_stop, all
//     go through setValve/setValveMask/allOff below.
//   - "all relays off" (fail-safe / IN1 LOW / boot) really means all
//   - any combination of valves is allowed, no simultaneity limit
//     enforced here (that's a farmer decision, informed by pressure
//     alerts elsewhere — not this class's job)

class IrrigationController {
public:
  static constexpr uint8_t CH_PUMP = 0;
  static constexpr uint8_t CH_DOSING = 1;
  static constexpr uint8_t CH_VALVE_BASE = 2;
  static constexpr uint8_t VALVE_COUNT = 4;

  // Named channels, per your request — "Main Pump", "Dosing Pump",
  // "Valve1".."Valve4" — matching RL1-RL6 in that order. Purely for
  // logging/UI readability; the numeric CH_* constants above are what
  // the rest of this class actually uses.
  static constexpr const char* CHANNEL_NAMES[6] = {
    "Main Pump", "Dosing Pump", "Valve1", "Valve2", "Valve3", "Valve4"
  };

  explicit IrrigationController(RelayController& relays) : _relays(relays) {}

  void begin() { _relays.begin(); allOff(); }

  // valveIndex is 0..3, mapping to RL3..RL6.
  void setValve(uint8_t valveIndex, bool on) {
    if (valveIndex >= VALVE_COUNT) return;
    _relays.setChannel(CH_VALVE_BASE + valveIndex, on);
    _syncPump();
  }

  bool getValve(uint8_t valveIndex) const {
    if (valveIndex >= VALVE_COUNT) return false;
    return _relays.getChannel(CH_VALVE_BASE + valveIndex);
  }

  // Apply a bitmask in one shot (bit N = valve N), e.g. for starting a
  // sequence that opens several valves together.
  void setValveMask(uint8_t mask) {
    for (uint8_t i = 0; i < VALVE_COUNT; i++) {
      _relays.setChannel(CH_VALVE_BASE + i, (mask & (1 << i)) != 0);
    }
    _syncPump();
  }

  uint8_t getValveMask() const {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < VALVE_COUNT; i++) {
      if (_relays.getChannel(CH_VALVE_BASE + i)) mask |= (1 << i);
    }
    return mask;
  }

  // Refuses to turn on with no valve open — see the class-level note.
  // Silently ignored rather than erroring: callers (manual_set, the
  // scheduler's dosing timer) don't need special-case handling for a
  // condition that should just never result in dosing running.
  void setDosing(bool on) {
    if (on && getValveMask() == 0) return;
    _relays.setChannel(CH_DOSING, on);
  }
  bool getDosing() const { return _relays.getChannel(CH_DOSING); }

  bool getPump() const { return _relays.getChannel(CH_PUMP); }

  // Fail-safe entry point: power loss (IN1 LOW), boot, or any other
  // "stop everything now" condition. Deliberately doesn't call
  // setValve/setDosing individually — one relays.allOff() covers pump,
  // dosing, and all four valves in a single call, so there's no window
  // where some relays are off and others haven't caught up yet.
  void allOff() { _relays.allOff(); }

private:
  void _syncPump() {
    bool anyValveOpen = (getValveMask() != 0);
    _relays.setChannel(CH_PUMP, anyValveOpen);
    // The other half of the dosing invariant: setDosing() blocks turning
    // it ON with nothing open, but a valve can close WHILE dosing is
    // already running (last valve manually shut, a sequence ending,
    // etc.) — this is what catches that and turns dosing off with it.
    if (!anyValveOpen) _relays.setChannel(CH_DOSING, false);
  }

  RelayController& _relays;
};
