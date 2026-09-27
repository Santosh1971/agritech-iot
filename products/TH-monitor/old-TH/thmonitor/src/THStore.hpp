#pragma once

#include <Arduino.h>

struct THRec {
  float temp = -1;
  float hum = -1;

  String toString() {
    return String("{") + temp + "," + hum + "}";
  }
};

using Time = long;

struct THRow {
  THRec rec;
  Time time;
  String toString() {
    return String("{") + "@" + time + "}";
  }
};

#include "SPIFFSStore.hpp"
using THStore = SPIFFSStore<THRow>;
