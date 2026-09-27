#include "myWiFi.hpp"

#define DEBUG
#include "debug.hpp"

MyWiFi::MyWiFi(MyWiFiManager& myWiFiManager): myWiFiManager(myWiFiManager) {
}

bool MyWiFi::connect() {
  // attempt auto-connect on first attempt when nothing is configured
  auto autoCon = firstTime;
  firstTime = false;

// if SSID is present, do not attempt autoCon
#ifdef WIFI_SKIP_AUTOCON_WITH_SSID
  autoCon = autoCon && !(myWiFiManager.id() == "");
#endif
  return myWiFiManager.connect(autoCon);
}

bool MyWiFi::connected() {
  return myWiFiManager.connected();
}

void MyWiFi::loop() {
  myWiFiManager.loop();
}

IPAddress MyWiFi::localIP() {
  return myWiFiManager.localIP();
}

Client& MyWiFi::client() {
  return myWiFiManager.wiFiClient();
}
