#include "myDHT.hpp"

#include "debug.hpp"

bool MyDHT::setup() {
  TRACE(F("MyDHT"), F("setup using library"), DHTNEW_LIB_VERSION);
  return true;
}

bool MyDHT::read(float& temp, float& humi) {
  yield();

  uint32_t start = micros();
  int chk = sensor.read();
  uint32_t stop = micros();

  switch (chk) {
    case DHTLIB_OK:
      break;
    default:
      WARN(F("MyDHT::read error"), chk, (stop - start), F("micros"));
      return false;
  }

  temp = sensor.getTemperature();
  humi = sensor.getHumidity();
  TRACE("MyDHT::read", temp, humi, sensor.getType(), (stop - start), F("micros"));
  return true;
}
