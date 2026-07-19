#pragma once
#include "Config.h"
#include <Arduino.h>

class RelayControl {
public:
    void begin(uint8_t pin, uint8_t ledPin = 255);  // 255 = no LED wired
    void loop();                                     // drives non-blocking test pulse timeout
    void on();
    void off();
    bool isOn();
    void testPulse(uint16_t durationMs = 5000);  // non-blocking — auto-off handled in loop()

private:
    uint8_t  _pin;
    uint8_t  _ledPin = 255;
    bool     _hasLed = false;
    bool     _state  = false;

    bool     _testPulseActive = false;
    uint32_t _testPulseOffAt  = 0;
};
