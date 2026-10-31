#include "LEDManager.h"

// Boards whose packet/flow LED is lit when the pin is LOW (ESP32-C3 Super
// Mini blue LED) define FLOW_LED_ACTIVE_LOW. Default: lit when HIGH.
#ifdef FLOW_LED_ACTIVE_LOW
#define FLOW_LED_ON  LOW
#define FLOW_LED_OFF HIGH
#else
#define FLOW_LED_ON  HIGH
#define FLOW_LED_OFF LOW
#endif
// Same for the WiFi/status LED (WIFI_LED_ACTIVE_LOW).
#ifdef WIFI_LED_ACTIVE_LOW
static inline void wifiWrite(int v) { digitalWrite(WIFI_LED_PIN, v ? LOW : HIGH); }
#else
static inline void wifiWrite(int v) { digitalWrite(WIFI_LED_PIN, v ? HIGH : LOW); }
#endif

void LEDManager::begin() {
    pinMode(WIFI_LED_PIN,        OUTPUT);
    pinMode(FLOW_SENSOR_LED_PIN, OUTPUT);
    wifiWrite(LOW);
    digitalWrite(FLOW_SENSOR_LED_PIN, FLOW_LED_OFF);
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
    digitalWrite(FLOW_SENSOR_LED_PIN, FLOW_LED_ON);
    _flowLedOn = true;
}

void LEDManager::_updateWiFiLED() {
    uint32_t now = millis();
    switch (_wifiState) {
        case WIFI_LED_OFF:
            wifiWrite(LOW);
            break;
        case WIFI_LED_SEARCHING:
            if (_wifiLedOn && now - _wifiLastMs >= 200) {
                wifiWrite(LOW);
                _wifiLedOn = false; _wifiLastMs = now;
            } else if (!_wifiLedOn && now - _wifiLastMs >= 200) {
                wifiWrite(HIGH);
                _wifiLedOn = true; _wifiLastMs = now;
            }
            break;
        // Same two SoftAP patterns as WPC (see updateWifiLed() in
        // WPC master_node): slow 1000/1000 when idle, fast 50/50 while a
        // phone is on the AP.
        case WIFI_LED_AP_IDLE:
            if (now - _wifiLastMs >= 1000) {
                _wifiLedOn = !_wifiLedOn;
                wifiWrite(_wifiLedOn ? HIGH : LOW);
                _wifiLastMs = now;
            }
            break;
        case WIFI_LED_AP_CLIENT:
            if (now - _wifiLastMs >= 50) {
                _wifiLedOn = !_wifiLedOn;
                wifiWrite(_wifiLedOn ? HIGH : LOW);
                _wifiLastMs = now;
            }
            break;
        case WIFI_LED_WIFI_ONLY:
            if (_wifiLedOn && now - _wifiLastMs >= 1000) {
                wifiWrite(LOW);
                _wifiLedOn = false; _wifiLastMs = now;
            } else if (!_wifiLedOn && now - _wifiLastMs >= 1000) {
                wifiWrite(HIGH);
                _wifiLedOn = true; _wifiLastMs = now;
            }
            break;
        case WIFI_LED_FULL_OK:
            if (now - _wifiLastMs >= (_wifiStep == 4 ? 1000 : 100)) {
                _wifiLastMs = now;
                _wifiStep++;
                if (_wifiStep > 4) _wifiStep = 0;
                bool on = (_wifiStep == 0 || _wifiStep == 2);
                wifiWrite(on ? HIGH : LOW);
                _wifiLedOn = on;
            }
            break;
    }
}

void LEDManager::_updateFlowLED() {
    if (_flowLedOn && millis() - _flowLastPulse >= 50) {
        digitalWrite(FLOW_SENSOR_LED_PIN, FLOW_LED_OFF);
        _flowLedOn = false;
    }
}
