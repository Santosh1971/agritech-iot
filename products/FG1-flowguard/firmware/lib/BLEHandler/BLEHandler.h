#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include "Config.h"

class BLEHandler {
public:
    void begin();
    void loop();
    bool isConnected();
    void sendResponse(const char* cmd, bool ok, const char* payload = "{}");
    void stopAdvertising();

    // Callbacks — set by main.cpp to wire into other modules
    std::function<void(const char* ssid, const char* pass)>    onWiFiConfig;
    std::function<void(const char* broker, uint16_t port,
                       const char* user, const char* pass)>    onMQTTConfig;
    std::function<void(uint32_t unixTime)>                     onRTCSync;
    std::function<void(uint32_t pulsesPerLiter)>               onCalibration;
    std::function<void()>                                       onRelayTest;
    std::function<void()>                                       onFactoryReset;
    std::function<String()>                                     onGetDeviceInfo;
    std::function<String()>                                     onWiFiScan;

private:
public:
    void _processCommand(const String& json);
    BLEServer*          _server    = nullptr;
    BLECharacteristic*  _txChar    = nullptr;
    bool                _connected = false;
};
