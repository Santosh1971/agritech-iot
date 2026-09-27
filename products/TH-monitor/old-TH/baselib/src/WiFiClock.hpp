#pragma once

#include "timeUtils.hpp"

/**
 * Clock based on WiFi via NTP server
 * It is to be called thriftily as needed by CalendarUtils, since costly.
 */
class WiFiClock: public MyClock {
  public:
    Time now();
};
