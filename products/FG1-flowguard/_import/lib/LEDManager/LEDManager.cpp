#include "LEDManager.h"

void LEDManager::begin() {
    pinMode(WIFI_LED_PIN,        OUTPUT);
    pinMode(FLOW_SENSOR_LED_PIN, OUTPUT);
    digitalWrite(WIFI_LED_PIN,        LOW);
    digitalWrite(FLOW_SENSOR_LED_PIN, LOW);
    Serial.println("[LED] Initialized");
}

void LEDManager::loop() {
    _updateWiFiLED();
    _updateFlowLED();
}

void LEDManager::setWiFiState(WiFiLEDState state) {
    if (_wifiState != state) {
        _wifiState  = state;
        _wifiStep   = 0;
        _wifiLastMs = millis();
    }
}

void LEDManager::flowPulse() {
    _flowLastPulse = millis();
    digitalWrite(FLOW_SENSOR_LED_PIN, HIGH);
    _flowLedOn = true;
}

void LEDManager::_updateWiFiLED() {
    uint32_t now = millis();
    switch (_wifiState) {
        case WIFI_LED_OFF:
            digitalWrite(WIFI_LED_PIN, LOW);
            break;
        case WIFI_LED_SEARCHING:
            if (_wifiLedOn && now - _wifiLastMs >= 200) {
                digitalWrite(WIFI_LED_PIN, LOW);
                _wifiLedOn = false; _wifiLastMs = now;
            } else if (!_wifiLedOn && now - _wifiLastMs >= 200) {
                digitalWrite(WIFI_LED_PIN, HIGH);
                _wifiLedOn = true; _wifiLastMs = now;
            }
            break;
        case WIFI_LED_WIFI_ONLY:
            if (_wifiLedOn && now - _wifiLastMs >= 1000) {
                digitalWrite(WIFI_LED_PIN, LOW);
                _wifiLedOn = false; _wifiLastMs = now;
            } else if (!_wifiLedOn && now - _wifiLastMs >= 1000) {
                digitalWrite(WIFI_LED_PIN, HIGH);
                _wifiLedOn = true; _wifiLastMs = now;
            }
            break;
        case WIFI_LED_FULL_OK:
            if (now - _wifiLastMs >= (_wifiStep == 4 ? 1000 : 100)) {
                _wifiLastMs = now;
                _wifiStep++;
                if (_wifiStep > 4) _wifiStep = 0;
                bool on = (_wifiStep == 0 || _wifiStep == 2);
                digitalWrite(WIFI_LED_PIN, on ? HIGH : LOW);
                _wifiLedOn = on;
            }
            break;
    }
}

void LEDManager::_updateFlowLED() {
    if (_flowLedOn && millis() - _flowLastPulse >= 50) {
        digitalWrite(FLOW_SENSOR_LED_PIN, LOW);
        _flowLedOn = false;
    }
}
