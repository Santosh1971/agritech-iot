#include "RelayControl.h"

void RelayControl::begin(uint8_t pin, uint8_t ledPin) {
    _pin = pin;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    _ledPin = ledPin;
    _hasLed = (ledPin != 255);
    if (_hasLed) {
        pinMode(_ledPin, OUTPUT);
        digitalWrite(_ledPin, LOW);
    }
    _state = false;
    Serial.printf("[RELAY] Initialized on pin %d%s\n", _pin,
                  _hasLed ? " (LED linked)" : "");
}

void RelayControl::on() {
    if (!_state) {
        digitalWrite(_pin, HIGH);
        if (_hasLed) digitalWrite(_ledPin, HIGH);
        _state = true;
        Serial.println("[RELAY] ON");
    }
}

void RelayControl::off() {
    if (_state) {
        digitalWrite(_pin, LOW);
        if (_hasLed) digitalWrite(_ledPin, LOW);
        _state = false;
        Serial.println("[RELAY] OFF");
    }
}

bool RelayControl::isOn() {
    return _state;
}

// Non-blocking: turns the relay on now, arms a millis()-based deadline,
// and returns immediately. loop() (called every main-loop iteration)
// turns it back off once the deadline passes — no delay() in the caller's
// path, so LEDs/WS/scheduler keep running during the test pulse.
void RelayControl::testPulse(uint16_t durationMs) {
    Serial.printf("[RELAY] Test pulse %dms (non-blocking)\n", durationMs);
    on();
    _testPulseActive = true;
    _testPulseOffAt  = millis() + durationMs;
}

void RelayControl::loop() {
    if (_testPulseActive && (int32_t)(millis() - _testPulseOffAt) >= 0) {
        _testPulseActive = false;
        off();
    }
}
