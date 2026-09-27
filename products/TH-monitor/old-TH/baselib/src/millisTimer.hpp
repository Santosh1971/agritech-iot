#pragma once

#include <Arduino.h>

/**
 * millis() can roll over every 49.5 days.
 * Interval check is more reliable than comparison, so long as it's less than 49.5/2
 * See https://arduino.stackexchange.com/questions/12587/how-can-i-handle-the-millis-rollover
 */

//#include <time.h>

class MillisTimer {
  unsigned long interval;
  unsigned long startTimeout;

  public:
    MillisTimer(unsigned long interval): interval(interval), startTimeout(0) {
    }

    void start() {
      startTimeout = millis();
    }

    /*
     * Returns the time when it started; * useful to find how long timer ran
     * If 0: indicates it wasnt running
     */
    unsigned long stop() {
      auto tmp = startTimeout;
      startTimeout = 0;
      return tmp;
    }

    unsigned long startedAt() {
      return startTimeout;
    }

    /**
     * @ return #milli seconds since start of timer; -1 if not started
     */
    unsigned long counter() {
      if (startTimeout <= 0)
        return -1;
      return millis() - startTimeout;
    }

    /*
     * 3 states: stopped, running or elapsed. Mutually exclusive.
     */
    enum State {
      Stopped = 'S',
      Running = 'R',
      Elapsed = 'E'
    };
    State getState() {
      if (startTimeout == 0)
        return Stopped;
      if ((millis() - startTimeout) < interval)
        return Running;
      return Elapsed;
    }

    bool elapsed() {
      return getState() == Elapsed;
    }

    bool running() {
      return getState() == Running;
    }

    bool stopped() {
      return getState() == Stopped;
    }

    String toString() {
      return String("{") + interval + "," + startTimeout + "," + getState() + "}";
    }
};
