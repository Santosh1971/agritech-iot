#pragma once

#include <functional>

#include "timer.h"  // internally uses it
#include "calendarUtils.hpp"

class MyTimer {
  static const int MAX_TIMERS = 20 * 2 + 10; // 20 channels, 20 schedules and 10 miscellaneous

  using Tiimer = Timer<MAX_TIMERS>; // use millis only
  Tiimer timers;

public:
    /*
     * Handle to check a timer after it is started.
     * It is returned by at/after/repeat calls below
     * - If too many timers, a null handle is retuned.
     * It is a function; so call it() with below parameters:
     * @stop: if true, stop the timer if still active.
     * @return: true if timer is still active/running; false, if it had fired
     *     in case of stop=true, timer will not fire again
     */
  //using Handle = Timer<max_timers, now>::Handle;
  // using Handle = std::function<bool (bool stop)>;
  //typedef Timer<max_timers, now>::Handle Handle;
  using Handle = Tiimer::Handle;

  // call function at specified time
  Handle at(Time time, std::function<void ()> funct);

  // call function after delay seconds
  Handle after(long delay, std::function<void ()> funct);

  // repeatedly call function every interval seconds, till funct returns false
  Handle repeat(long interval, std::function<bool ()> funct);

  void loop();
};

extern MyTimer timers;
