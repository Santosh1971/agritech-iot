#pragma once
#include "Config.h"
#include <Arduino.h>

class FlowSensor {
public:
    void begin(uint8_t pin);
    void setCalibration(uint32_t pulsesPerLiter);
    uint32_t getCalibration();
    float getLitersDelivered();
    void resetCount();
    void IRAM_ATTR pulseISR();   // call from global ISR

private:
    volatile uint32_t _pulseCount = 0;
    uint32_t _pulsesPerLiter = 450;
    uint8_t _pin;
};

