#pragma once

#include <Ticker.h>
#include "myBitset.hpp"

class LEDController {
  public:
    LEDController(int pin, bool disabled=false);

    LEDController(const LEDController&) = delete;
    void operator=(const LEDController&) = delete;

    /**
     * setup
     * @onOff: initial value
     * @test: if true, do a test for a second; note: adds a delay of a second
     */
    void setup(bool onOff=true, bool test=true);

    /**
    * turn LED on or off
    */
    void set(bool onOff=true);

    /**
     * set blinking at a rate and pattern
     * @pattern: defaults to "01"; can be "000001", "0101100" etc. it will turn
     * on in the "1" slots and off in the 0 slots
     * @rate: slots are flashed at rate; use FAST/SLOW/MEDIUM for typical rates
     */
    void set(float rate, const char* pattern=nullptr);

    const int pin;
    const bool disabled;

    const float SLOW = 2.0; // common rates
    const float MEDIUM = 1.0;
    const float FAST = 0.2;

  protected:
    // overridable methods for use with child versions
    virtual void initialize(); // initial setup
    virtual void put(bool onOff);  // output on/off
    virtual bool get();  // return current state

  private:
    // internal structures and routines used for blinking
    Ticker ticker;
    struct {
      MyBitset bits;
      uint8_t nobits;
      int nextIx;
    } tickerStruct;
    void tick();  // called by the ticker

    bool isOn;  // cached state of LED

    /**
     * write the new state to LED pin
     * cache it to avoid superfluous writes
     * @return true if actually written (ie. changed LED state)
     */
    bool write(bool onOff);
};
