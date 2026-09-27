#pragma once

#include "timeUtils.hpp"

#include "myGsm.hpp"

class GsmClock: public MyClock {
  MyGsm& myGsm;
  bool clockSet = false;
  const char* setup();

  public:
    GsmClock(MyGsm&);
    Time now();
};
