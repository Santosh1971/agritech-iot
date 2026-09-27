#pragma once

#include <Arduino.h>

#include <RTClib.h>
/*
 * A few structures
 */
using YyyyMmDd=DateTime; // 6 bytes

struct HhMm {
  uint8_t hh, mm;

  String toString() {
    return hh + String(":") + mm;
  }
};

#include "timeUtils.hpp"

class CalendarUtils {
public:
  /**
   * reference clock to synchronize with
   * - RTC (local clock) where available
   * - can use external clock (NTP) via GSM/WiFi when not, with less reliability
   */
  static MyClock* clock;

  /**
   * setup the utils; should be called before other methods are called
   * adjustTime: is used (along with millis) to return now()
   * synchronized: is from clock or another valid clock; else, it is a fallback
   */
  static bool setup(MyClock* clock, Time adjustTime, bool synchronized);

  static Time now();

  static YyyyMmDd today();

  static YyyyMmDd add(YyyyMmDd date, int incr=1);

  static YyyyMmDd toYyyyMmDd(Time t);

  static HhMm toHhMm(Time t);

  static Time toTime(YyyyMmDd& yyyyMmDd, HhMm& hhMm);

  static Time toTime(YyyyMmDd& yyyyMmDd);

  static long diff(Time one, Time two);

  static long diff(YyyyMmDd one, YyyyMmDd two);

  static Time add(Time base, long offset=0) {
    return base + offset;
  }

  // essential to make add() accept multiple offsets
  template<typename Arg, typename... Args>
  static Time add(Time base, Arg offset, Args... offsets) // recursive variadic function
  {
    Time b2 = base + offset;
    return add(b2, offsets...);
  }

  /*
   * @return true if t falls in date
   */
  static bool equals(Time t, YyyyMmDd& date);

  static String toString(DateTime t, bool timealso=false);

  static String toString(Time t, bool timealso=true);

  /**
   * synchronize internal variables with clock
   * @force: if true, synchronize in any case; else, do only if as per periodicity defined
   * @return: true if successful or skipped as per periodicity; false if failed
   */
  static bool synchronize(bool force=false);

  /**
   * return true if internal variables have been synchronized successfully with
   * clock
   * @return true
   */
  static bool synchronized();

  // for debugging: return current discrepancy with RTC reading
  static long clockDiscrepancy();

  CalendarUtils() = delete; // disallow instantiation
};

bool operator==(DateTime&one, DateTime&oth);
bool operator!=(DateTime&one, DateTime&oth);
