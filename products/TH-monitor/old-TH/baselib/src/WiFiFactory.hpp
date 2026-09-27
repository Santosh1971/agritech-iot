#pragma once

#include "platform.hpp"
#include "myWiFiManager.hpp"
#include "myWiFi.hpp"

#include "WiFiClock.hpp"

/**
 * Factory class for WiFi
 */
class WiFiFactory: public Platform::Factory {
    MyWiFiManager& myWiFiManager;
    MyWiFi myWiFi;

    WiFiClock clock;

  public:
    WiFiFactory(MyWiFiManager& myWiFiManager);

    MyClient& getClient();

    /**
     * Get a clock which can get time as set in WiFi
     */
    MyClock& getClock();

    /*
     * Make Manager for GSM based OTA from OTA URL for the file
     */
    OTAManager* makeOTAManager(const char* otaUrl, const char* filename);

    /**
     * Provide access for special purposes (such as AP mgmt etc.)
     */
    MyWiFiManager& _myWiFiManager() {
      return myWiFiManager;
    }
};
