#pragma once

#include "myGsm.hpp"
#include "platform.hpp"

#include "GsmClock.hpp"

/**
 * Factory class for GSM
 */
class GsmFactory: public Platform::Factory {
    MyGsm& myGsm;

    GsmClock clock;

  public:
    GsmFactory(MyGsm& myGsm);

    MyClient& getClient();

    /**
     * Get a clock which can get time as set in GSM
     */
    MyClock& getClock();

    /*
     * Make Manager for GSM based OTA from OTA URL for the file
     */
    OTAManager* makeOTAManager(const char* otaUrl, const char* filename);

    /**
     * Provide access for special purposes
     */
    MyGsm& _myGsm() {
      return myGsm;
    }
};
