#include "link_ble.h"

#include <NimBLEDevice.h>

namespace ble {
namespace {

NimBLEServer* server = nullptr;
NimBLECharacteristic* tx = nullptr;
String incoming;          // bytes received so far
String ready;             // one complete line waiting for loop()
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    portENTER_CRITICAL(&lock);
    for (char ch : v) {
      if (ch == '\n') {
        if (ready.length() == 0) ready = incoming;  // drop a line if loop() hasn't taken the last one
        incoming = "";
      } else if (incoming.length() < 8192) {
        incoming += ch;
      }
    }
    portEXIT_CRITICAL(&lock);
  }
};

}  // namespace

void begin(const String& deviceName) {
  NimBLEDevice::init(deviceName.c_str());
  NimBLEDevice::setMTU(247);
  server = NimBLEDevice::createServer();
  NimBLEService* svc = server->createService(SERVICE_UUID);
  tx = svc->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rx = svc->createCharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
}

bool connected() {
  return server && server->getConnectedCount() > 0;
}

bool takeLine(String& out) {
  bool got = false;
  portENTER_CRITICAL(&lock);
  if (ready.length()) {
    out = ready;
    ready = "";
    got = true;
  }
  portEXIT_CRITICAL(&lock);
  return got;
}

void sendLine(const String& line) {
  if (!connected() || !tx) return;
  size_t mtu = 23;
  std::vector<uint16_t> peers = server->getPeerDevices();
  if (!peers.empty()) mtu = server->getPeerMTU(peers[0]);
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  String all = line + "\n";
  for (size_t i = 0; i < all.length(); i += chunk) {
    size_t n = min(chunk, all.length() - i);
    tx->setValue((const uint8_t*)all.c_str() + i, n);
    tx->notify();
    delay(4);  // let the stack drain; phones drop notifications sent back to back
  }
}

}  // namespace ble
