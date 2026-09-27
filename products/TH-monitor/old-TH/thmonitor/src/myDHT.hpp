#pragma once

#include <dhtnew.h>

class MyDHT {
  DHTNEW sensor;

 public:
  MyDHT(uint8_t pin) : sensor(pin) {
  }

  bool setup();

  /**
   * reads temperature and humidity into the variables passed
   * @return true if sucessful
   */
  bool read(float& temp, float& humi);
};

extern MyDHT myDHT;
