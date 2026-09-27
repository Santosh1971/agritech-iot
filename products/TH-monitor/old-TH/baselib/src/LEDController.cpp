#include "LEDController.hpp"

#include <Arduino.h>

#include <functional>

#define DEBUG
#include "debug.hpp"

LEDController::LEDController(int pin, bool disabled):
  pin(pin), disabled(disabled), isOn(false) {
  // TRACE(F("LED"), pin, disabled); Serial will not be set at this time
}

void LEDController::setup(bool onOff, bool test) {
  TRACE(F("LED::initialize"), pin, disabled, onOff, test);

  if (!disabled) {
    initialize();
    this->isOn = get();
  }

  if (test) {
    write(!onOff);
    delay(1000);
  }

  write(onOff);
}

void LEDController::set(bool onOff) {
  TRACE(F("LED::set"), pin, onOff);

  write(onOff);

  ticker.detach();
}

#include <functional>

void LEDController::set(float rate, const char* pattern) {
  if (pattern == nullptr)
    pattern = "01";

  TRACE(F("LED::set"), pin, rate, pattern);

  auto len = ::strlen(pattern);
  if (len <= 0 || len >= 32) {
    WARN("Invalid pattern", pattern, "falling back on 01");
    pattern = "01";
    len = ::strlen(pattern);
  }

  tickerStruct = { MyBitset(pattern), (uint8_t)len, 0 };

  const auto myself = this;
  ticker.attach(rate, [myself]() {
        myself->tick();
      });
}

bool LEDController::write(bool onOff) {
  if (onOff == isOn) // already same
    return false;

  if (!disabled) {
    put(onOff);
  }

  // TRACE(F("LED::write"), pin, onOff);

  this->isOn = onOff;
  return true;
}

void LEDController::initialize() {
  pinMode(pin, OUTPUT);           // Set Indicator LED as output
}

void LEDController::put(bool onOff) {
  pinMode(pin, OUTPUT);  // Set Indicator LED as output; CHECKME: seems required
  digitalWrite(pin, onOff? HIGH: LOW);
  // TRACE(F("LED::put"), pin, onOff);
}

bool LEDController::get() {
  return digitalRead(pin) == HIGH;
}

void LEDController::tick() {
  int ix = tickerStruct.nextIx++;
  if (ix >= tickerStruct.nobits) {
    ix = tickerStruct.nextIx = 0;
  }

  bool bit = tickerStruct.bits.get(ix);
  //TRACE(F("LED::Tick#"), pin, ix, nextIx, nobits, bits.asString(), bit);

  write(bit);
}
