#pragma once

#include "LEDController.hpp"

/**
 * Variation of LEDController, which uses Shift-Registers to access pin
 * Uses a single global SR. TODO: parameterize when multiple SR might exist
 */
class LEDSRController: public LEDController {
  public:
    LEDSRController(int pin, bool disabled=false);

    /**
     * test the SR as a whole; set state of all pins to @finalVal
     * @notes: adds a 1-sec delay
     */
    static void test(bool finalVal = false);

  protected:
    // overridable methods for use with shift-register versions
    virtual void initialize(); // initial setup
    virtual void put(bool onOff);  // output on/off
    virtual bool get();  // return current state
};
