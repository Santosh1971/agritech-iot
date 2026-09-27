#pragma once

#include <Arduino.h>

#include "OTAManager.hpp"
#include "myGsm.hpp"

/**
 * OTA for GSM
 */
class GsmOTAManager: public OTAManager {
    MyGsm& myGsm;

  public:
    GsmOTAManager(MyGsm& myGsm, const char* otaUrl, const char* filename);

    void setup(); // call once during setup

    void start(const char* filename=nullptr, Messenger messenger=nullptr); // call once to start the OTA process

    void loop(void);  // should be called from loop
};
