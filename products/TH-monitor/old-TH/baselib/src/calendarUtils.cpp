#include "calendarUtils.hpp"

#define DEBUG
#include "debug.hpp"

/*
 * rtc/ntp cannot be called too frequently. So, we combine with millis(),
 * with a baseTime (occasionally sync'ed with RTC/NTP) and baseMillis to
 * compute now(). Millis rolls over in 40+ days: so this should be sufficient.
 *
 * Other routines to compute differences etc.
 */
static struct {
  Time time; // base Time when synched with clock
  unsigned long millis; // millis counter when last sync'ed
  bool synchronized;  // was it synchronized?
  String toString() {
    return String("{") + time + "," + millis + "," + synchronized + "}";
  }
} syncBase = { 0, 0, false };

// synchronization interval with clock
const unsigned long UNSYNC_INTERVAL = 10*60*1000; // 10 minutes: until synchronized
const unsigned long RESYNC_INTERVAL = 12*60*60*1000; // 12 hours: resync after synchronized

// clock MUST be set via setup
MyClock* CalendarUtils::clock = nullptr;

bool CalendarUtils::synchronize(bool force) {
  bool needed = force || (millis() - syncBase.millis > (syncBase.synchronized? RESYNC_INTERVAL: UNSYNC_INTERVAL));
  if (!needed) {
    // TRACE("Synchronization not needed", force, syncBase.toString(), millis());
    return true;
  }

  auto newTime = clock->now();
  if (newTime <= 0) {
    WARN("Synchronize with clock failed", newTime);
    // keep last values
    return false;
  }

  syncBase = { newTime, millis(), true };
  TRACE("Synchronized with clock", toString(newTime));
  return true;
}

bool CalendarUtils::synchronized() {
  return syncBase.synchronized;
}

bool CalendarUtils::setup(MyClock* clock, Time adjustTime, bool synchronized) {
  CalendarUtils::clock = clock;

  TRACE("CalendarUtils::setup", toString(adjustTime), synchronized);
  syncBase = { adjustTime, millis(), synchronized };

#if 0
  syncBase = { fallbackTime, millis(), false }; // yet to be synchronized
  // do one computation; test discrepancy
  auto n = now();
  TRACE("Time:" + toString(n));
  auto discrepancy = clockDiscrepancy();
  if (discrepancy > 5)  // tolerate upto 5 seconds
    WARN("Discrepancy with Clock", discrepancy, "Clock", toString(syncBase.time), "Computed", toString(n));
#endif

  return true;
}

#include "millisTimer.hpp"

Time CalendarUtils::now()
{
  synchronize();

  Time t = syncBase.time + (millis()-syncBase.millis)/1000;
#if 0
  TRACE("Millis Based time", CalendarUtils::toString(syncBase.time),
        CalendarUtils::toString(t), "Diff(sec)",
        CalendarUtils::diff(clock->now(), t));
#endif
  return t;
}

YyyyMmDd CalendarUtils::today()
{
  return toYyyyMmDd(now());
}

static const long SECS_PER_DAY = 24*60*60;  // FIXME: doesnt work with daylight savings
YyyyMmDd CalendarUtils::add(YyyyMmDd date, int incr) {
  Time t = toTime(date);
  t = add(t, incr*SECS_PER_DAY);
  return toYyyyMmDd(t);
}

YyyyMmDd CalendarUtils::toYyyyMmDd(Time t)
{
  DateTime date(t);
  return {date.year(), date.month(), date.day()};
}

HhMm CalendarUtils::toHhMm(Time t)
{
  DateTime date(t);
  return {date.hour(), date.minute()};
}

Time CalendarUtils::toTime(YyyyMmDd &yyyyMmDd, HhMm &hhMm)
{
  return DateTime(yyyyMmDd.year(), yyyyMmDd.month(), yyyyMmDd.day(), hhMm.hh, hhMm.mm).unixtime();
}

Time CalendarUtils::toTime(YyyyMmDd &yyyyMmDd)
{
  return yyyyMmDd.unixtime();
}

long CalendarUtils::diff(Time one, Time two)
{
  return one - two;
}

long CalendarUtils::diff(YyyyMmDd one, YyyyMmDd two)
{
  return toTime(one) - toTime(two);
}

/*
   * @return true if t falls in date
   */
bool CalendarUtils::equals(Time t, YyyyMmDd &date)
{
  YyyyMmDd d = toYyyyMmDd(t);
  return toTime(d) == toTime(date);
}

#include <stdio.h>
static char buf[100];
static auto longFmt = "{%04d/%02d/%02d %02d:%02d:%02d}";
static auto shortFmt = "{%04d/%02d/%02d}";
String CalendarUtils::toString(DateTime t, bool timealso)
{
  if (timealso)
    ::sprintf(buf, longFmt, t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  else
    ::sprintf(buf, shortFmt, t.year(), t.month(), t.day());
  return String(buf);
}

String CalendarUtils::toString(Time t, bool timealso)
{
  if (t <= 0)
    return String("<nul>");
  return toString(DateTime(t), timealso);
}

long CalendarUtils::clockDiscrepancy() {
  Time now = CalendarUtils::now();
  return now - clock->now();
}

bool operator==(DateTime &one, DateTime &oth)
{
  return one.unixtime() == oth.unixtime();
}

bool operator!=(DateTime &one, DateTime &oth)
{
  return one.unixtime() != oth.unixtime();
}
