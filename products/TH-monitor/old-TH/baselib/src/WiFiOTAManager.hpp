#pragma once

#include <Arduino.h>

#include "OTAManager.hpp"
#include "myWiFiManager.hpp"

/**
 * OTA for GSM
 */
class WiFiOTAManager: public OTAManager {
    MyWiFiManager& myWiFiManager;

  public:
    WiFiOTAManager(MyWiFiManager& myWiFiManager, const char* otaUrl, const char* filename);

    void setup(); // call once during setup

    void start(const char* filename=nullptr, Messenger messenger=nullptr); // call once to start the OTA process

    void loop(void);  // should be called from loop
};
