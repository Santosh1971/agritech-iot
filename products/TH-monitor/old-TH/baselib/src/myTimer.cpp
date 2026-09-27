#include "myTimer.hpp"

//#define DEBUG
#include "debug.hpp"

#ifdef DEBUG
static String toString(Time t) {
  return CalendarUtils::toString(t, true);
}
#endif

MyTimer::Handle MyTimer::at(Time time, std::function<void()> funct) {
  auto delay = CalendarUtils::diff(time, CalendarUtils::now());

  return after(delay, funct);
}

MyTimer::Handle MyTimer::after(long delay, std::function<void()> funct) {
  TRACE(F("MyTimer::after added"), delay);

  if (delay <= 0) {
    WARN("delay too less", delay, "using 1ms");
  }

  auto h = timers.in((delay<=0)? 1: delay*1000, [funct, delay]() {
    TRACE(F("MyTimer::after fired"), delay, toString(CalendarUtils::now()));
        funct();
        return false;
     });

  if (!h.active()) {
    WARN(F("MyTimer::Too many timers: timer not added"));
  }

  return h;
}

MyTimer::Handle MyTimer::repeat(long interval, std::function<bool()> funct) {
  TRACE(F("MyTimer::repeat added"), interval);

  auto f = funct;
#ifdef DEBUG
  f = [funct, interval]() {
    TRACE(F("MyTimer::repeat fired"), interval, toString(CalendarUtils::now()));
    return funct();
  };
#endif

  auto h = timers.every(interval*1000, f);
  if (!h.active()) {
    WARN(F("MyTimer::Too many timers: timer not added"));
  }

  return h;
}

void MyTimer::loop() {
  //TRACE(F("MyTimer::loop:start"));
  timers.tick();
  //TRACE(F("MyTimer::loop:finish"));
}
