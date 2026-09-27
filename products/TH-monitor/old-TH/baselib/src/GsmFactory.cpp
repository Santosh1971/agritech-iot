#include "GsmFactory.hpp"

#define DEBUG
#include "debug.hpp"

GsmFactory::GsmFactory(MyGsm& myGsm): myGsm(myGsm), clock(myGsm) {
}

MyClient& GsmFactory::getClient() {
  return myGsm;
}

MyClock& GsmFactory::getClock() {
  return clock;
}

#ifdef ESP8266
OTAManager* GsmFactory::makeOTAManager(const char* otaUrl, const char* filename) {
  ERROR("GSM OTA Manager not supported for ESP8266");
  return nullptr;
}
#elif ESP32
#include "GsmOTAManager.hpp"
OTAManager* GsmFactory::makeOTAManager(const char* otaUrl, const char* filename) {
  return new GsmOTAManager(myGsm, otaUrl, filename);
}
#endif
