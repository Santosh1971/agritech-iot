#include "RTCClock.hpp"

static RTC_DS3231 rtc;

#define DEBUG
#include "debug.hpp"

RTCClock::RTCClock(MyClock* extClock, unsigned long attemptInterval):
  extClock(extClock), extClockAttempt(attemptInterval), begun(false) {
}

#include <Wire.h>
#include "platformConfig.hpp"

#include "miscUtils.hpp"

bool RTCClock::setup() {
  TRACE("RTCClock::setup");

  if (! rtc.begin()) {
    WARN("Couldn't find RTC");
    return false;
  }

  begun = true;
  return true;
}

bool RTCClock::synchronize(bool force, Time fallbackTime) {
  TRACE("RTCClock::synchronize", force, fallbackTime);

  if (!force) {
    if (!rtc.lostPower()) {
       TRACE("Neither force nor lostPower");
       return true;
    }
  }

  if (synchronize() > 0) {
    TRACE("Synchronized from clock");
    return true;
  }

  if (fallbackTime > 0) {
    TRACE("Setting clock to the preset value...", fallbackTime);
    adjust(fallbackTime);
  }

  return false;
}

Time RTCClock::synchronize() {
  TRACE("RTCClock::synchronize()");
  auto rc = getExtClock();
  if (rc < 0) {
    return rc;
  }

  adjust(rc);
  return rc;
}

Time RTCClock::now() {
  return now(true);
}

Time RTCClock::now(bool getExternal) {
  auto internal = _now();

  if ((internal >= 0) || !getExternal)
    return internal;

  auto external = getExtClock();
  if (external < 0) {
    WARN(F("RTC:: RTC and external clocks both failed"));
    return external;
  }

  adjust(external);
  return external;
}

Time RTCClock::getExtClock() {
#define RETURN(_msg) { \
  WARN("Sync with External Clock:", (_msg)); \
  return -1; \
}
  if (extClock == nullptr) {
    RETURN("no external clock");
  }

  if (extClockAttempt.running()) {  // already attempted and failed
    RETURN("frequent external clock calls");
  }

  auto rc = extClock->now();
  if (rc <= 0) {
    WARN("extClock returned", rc);
    extClockAttempt.start();  // to prevent too many extClock() calls
    RETURN("external clock call failed");
  }

  extClockAttempt.stop(); // if any
  return rc;
#undef RETURN
}

void RTCClock::adjust(Time now) {
  TRACE(F("RTC::Adjusting clock to"), now);
  if (!begun) {
    TRACE(F("...skipped"), begun);
    return;
  }
  rtc.adjust(DateTime(now));
}

Time RTCClock::_now() {
  if (!begun) {
    TRACE(F("_now: notbegun"), begun);
    return -2;
  }
  auto lost = rtc.lostPower();
  if (lost) {
    TRACE(F("_now: lostPower"), lost);
    return -1;
  }

  return rtc.now().unixtime();
}
