#pragma once
#include <Arduino.h>
#include "Config.h"

enum WiFiLEDState {
    WIFI_LED_OFF,
    WIFI_LED_SEARCHING,
    WIFI_LED_WIFI_ONLY,
    WIFI_LED_FULL_OK
};

class LEDManager {
public:
    void begin();
    void loop();
    void setWiFiState(WiFiLEDState state);
    void flowPulse();

private:
    void _updateWiFiLED();
    void _updateFlowLED();

    WiFiLEDState _wifiState     = WIFI_LED_OFF;
    uint32_t     _wifiLastMs    = 0;
    uint8_t      _wifiStep      = 0;
    bool         _wifiLedOn     = false;
    uint32_t     _flowLastPulse = 0;
    bool         _flowLedOn     = false;
};
