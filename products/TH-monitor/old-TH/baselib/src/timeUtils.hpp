#pragma once

using Time = long;

/**
 * CHECKME: Removal of...
 * - Coupling of Timezone handling in RTClib
 * - hardwiring to IST.
 * Suggest: Most time be handling in UTC.
 */
/**
 * #minutes present timezone is ahead of UTC; -ve if behind
 * e.g. 5.5*60 for IST
 */
extern const long tzShiftInMinutes;

/*
 * Clock is an object which provides current time.
 * @return time; -1 if it cannot
 *
 * Implementations:
 * * RTCClock: provides a local clock
 * * GSMFactory and WiFiFactory: provides access to external clock. THis is
 *   in turn used by RTCClock for syncing itself
 *
 * Clocks are not used directly. They are accessed via CalendarUtils, which
 * combines it with millis(), so that the clocks are used optimally.
 */
class MyClock {
  public:
    virtual Time now() = 0;

    static const char* defaultNTPServer;
};
