#include "WiFiFactory.hpp"

#define DEBUG
#include "debug.hpp"

WiFiFactory::WiFiFactory(MyWiFiManager& myWiFiManager):
  myWiFiManager(myWiFiManager), myWiFi(myWiFiManager) {
}

MyClient& WiFiFactory::getClient() {
  return myWiFi;
}

MyClock& WiFiFactory::getClock() {
  return clock;
}

#include "WiFiOTAManager.hpp"
OTAManager* WiFiFactory::makeOTAManager(const char* otaUrl, const char* filename) {
  return new WiFiOTAManager(myWiFiManager, otaUrl, filename);
}
