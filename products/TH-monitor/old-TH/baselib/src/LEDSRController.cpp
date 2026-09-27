#include "LEDSRController.hpp"

// #define DEBUG
#include "debug.hpp"

#include <ShiftRegister74HC595.h>
// create a global shift register object
// parameters: <number of shift registers> (data pin, clock pin, latch pin)
//WEMOS : DATA-D6 ,12, CLK - D1  , 5 , Latch- D5  ,14
static ShiftRegister74HC595<2> sr(19,18, 15); // 2 is no of shift register
                              // WhiteElephent TTGO PCB   Mar2021

LEDSRController::LEDSRController(int pin, bool disabled):
  LEDController(pin, disabled) {
}

void LEDSRController::initialize() {
}

void LEDSRController::put(bool onOff) {
#ifdef LEDSR_UPDATE_IMMEDIATE
  sr.set(pin, onOff? HIGH: LOW);
#else
  sr.setNoUpdate(pin, onOff? HIGH: LOW);
  sr.updateRegisters();
#endif
  // TRACE(F("LED::put"), pin, onOff);
}

bool LEDSRController::get() {
  return sr.get(pin) == HIGH;
}

static void setAll(bool onOff) {
  if (onOff)
    sr.setAllHigh();
  else
    sr.setAllLow();
}

void LEDSRController::test(bool finalVal) {
  TRACE("LEDSRController::test", finalVal);
  setAll(!finalVal);
  delay(500);
  setAll(finalVal);
  delay(500);
}
