#include "FlowSensor.h"


void FlowSensor::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    _pulseCount = 0;
    Serial.printf("[FLOW] Initialized on pin %d, cal=%lu pulses/L\n", _pin, _pulsesPerLiter);
}

void FlowSensor::setCalibration(uint32_t pulsesPerLiter) {
    if (pulsesPerLiter > 0) _pulsesPerLiter = pulsesPerLiter;
    Serial.printf("[FLOW] Calibration set: %lu pulses/L\n", _pulsesPerLiter);
}

uint32_t FlowSensor::getCalibration() {
    return _pulsesPerLiter;
}

float FlowSensor::getLitersDelivered() {
    uint32_t count;
    noInterrupts();
    count = _pulseCount;
    interrupts();
    return (float)count / (float)_pulsesPerLiter;
}

void FlowSensor::resetCount() {
    noInterrupts();
    _pulseCount = 0;
    interrupts();
}

void IRAM_ATTR FlowSensor::pulseISR() {
    _pulseCount++;
}
