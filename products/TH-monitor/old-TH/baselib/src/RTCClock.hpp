#pragma once

#include <Arduino.h>

#include <functional>

#include "timeUtils.hpp"

#include <RTClib.h>

#include "millisTimer.hpp"

/**
 * RTCClock wraps around RTC and extends it with the following
 * * takes an optional adjustTime on setup (such as that of the build) to initialize to
 * * takes an optional external clock to synchronize with when it lostPower().
 *   - when now() is called and it finds that it has lostPower(), it calls this extClock to get the new time
 *     - CAUTION: ExtClock() is now based on NTPUtils which can add 1-2 seconds.
 *     - if it fails, the time is not adjusted
 * * takes an attemptInterval: prevents multiple calls to external call within attemptInterval
 *
 * Whenever now() is called, and finds that it has lostPower(), will call external clock to adjust its time.
 * Allows for correction during setup itself.
 * Allow for manual correction.
 *
 * It is expected that this class is not used directly. It should be called via CalendarUtils.
 * This is because RTC calls are costly and frequent calls are destabilizing.
 */
class RTCClock: public MyClock {
  public:
    RTCClock(MyClock* extClock=nullptr, unsigned long attemptInterval=5000);

    /**
     * setup
     * @return true; false if couldnt be setup (RTC couldnt be found)
     */
    bool setup();

    /**
     * Synchronize RTC with external clock, or fall back to a fallbackTime.
     * This is needed for predictability, in case of failure conditions.
     * @force: normally, on lostPower, this will do nothing.
     *    if true, do the synchronize() even without lostPower
     * @fallbackTime: a backup initial instant to be adjusted to explicitly
     *    if nothing else is available; optional
     * @return true; false if couldnt be synchronized
     */
    bool synchronize(bool force, Time fallbackTime=0);

    /**
     * synchronize with external clock
     * @return time synchronized to; -1 if not successful
     */
    Time synchronize();

    /**
     * get current time
     * same as now(true)
     */
    Time now();

    /**
     * get current time
     * @getExternal: if true, if internal fails, try external also
     * @note: if external is received, internal is adjusted to that
     */
    Time now(bool getExternal);

    /*
     * adjust the current clock value to the value passed (now)
     */
    void adjust(Time now);

    bool hasBegun() {
      return begun;
    }

  private:
    /**
     * call external clock and adjust the time to it.
     * @return Time; -1 if couldnt get the time
     */
    Time getExtClock();
    MyClock* extClock; // used when rtc lostPower()
    MillisTimer extClockAttempt; // how frequently to reattempt if extClock fails

    Time _now();  // internal now()

    bool begun; // if begin() is successful (i.e. available)
};
