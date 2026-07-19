#include "BLEHandler.h"

// ---- BLE Server Callbacks ----
class ServerCB : public BLEServerCallbacks {
public:
    bool* connected;
    void onConnect(BLEServer*)    override { *connected = true;  Serial.println("[BLE] Client connected"); }
    void onDisconnect(BLEServer* s) override {
        *connected = false;
        Serial.println("[BLE] Client disconnected — restarting advertising");
        s->startAdvertising();
    }
};

// ---- BLE Write Callback ----
class WriteCB : public BLECharacteristicCallbacks {
public:
    BLEHandler* handler;
    void onWrite(BLECharacteristic* c) override {
        String val = c->getValue().c_str();
        if (val.length()) handler->_processCommand(val);
    }
};

void BLEHandler::begin() {
    BLEDevice::init(BLE_DEVICE_NAME);
    _server = BLEDevice::createServer();

    auto* scb = new ServerCB();
    scb->connected = &_connected;
    _server->setCallbacks(scb);

    BLEService* svc = _server->createService(BLE_SERVICE_UUID);

    // TX char — ESP32 → Phone (notify)
    _txChar = svc->createCharacteristic(
        BLE_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    _txChar->addDescriptor(new BLE2902());

    // RX char — Phone → ESP32 (write)
    BLECharacteristic* rxChar = svc->createCharacteristic(
        BLE_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    auto* wcb = new WriteCB();
    wcb->handler = this;
    rxChar->setCallbacks(wcb);

    svc->start();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.printf("[BLE] Advertising as '%s'\n", BLE_DEVICE_NAME);
}

void BLEHandler::loop() {
    // Nothing needed — callbacks handle everything
}

bool BLEHandler::isConnected() { return _connected; }

void BLEHandler::stopAdvertising() {
    // Only needed for initial provisioning — once WiFi/MQTT are up,
    // continued BLE advertising causes radio contention with WiFi on
    // ESP32's shared 2.4GHz radio, leading to periodic MQTT keepalive
    // drops. Safe to stop once the device is online and no BLE client
    // is actively connected.
    if (!_connected) {
        BLEDevice::stopAdvertising();
        Serial.println("[BLE] Advertising stopped — WiFi/MQTT priority mode");
    }
}

void BLEHandler::sendResponse(const char* cmd, bool ok, const char* payload) {
    if (!_connected || !_txChar) return;
    JsonDocument doc;
    doc["cmd"]     = cmd;
    doc["ok"]      = ok;
    doc["payload"] = serialized(payload);
    String out; serializeJson(doc, out);
    _txChar->setValue(out.c_str());
    _txChar->notify();
}

void BLEHandler::_processCommand(const String& json) {
    Serial.printf("[BLE] Received: %s\n", json.c_str());
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        sendResponse("error", false, "{\"msg\":\"invalid JSON\"}");
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) { sendResponse("error", false, "{\"msg\":\"no cmd\"}"); return; }

    if (strcmp(cmd, "wifi_config") == 0) {
        if (onWiFiConfig)
            onWiFiConfig(doc["ssid"] | "", doc["pass"] | "");
        sendResponse(cmd, true);

    } else if (strcmp(cmd, "mqtt_config") == 0) {
        if (onMQTTConfig)
            onMQTTConfig(doc["broker"] | "", doc["port"] | 1883,
                         doc["user"]   | "", doc["pass"] | "");
        sendResponse(cmd, true);

    } else if (strcmp(cmd, "rtc_sync") == 0) {
        if (onRTCSync) onRTCSync((uint32_t)doc["unix"]);
        sendResponse(cmd, true);

    } else if (strcmp(cmd, "calibrate") == 0) {
        if (onCalibration) onCalibration((uint32_t)doc["ppl"]);
        sendResponse(cmd, true);

    } else if (strcmp(cmd, "relay_test") == 0) {
        if (onRelayTest) onRelayTest();
        sendResponse(cmd, true);

    } else if (strcmp(cmd, "factory_reset") == 0) {
        sendResponse(cmd, true);
        delay(500);
        if (onFactoryReset) onFactoryReset();

    } else if (strcmp(cmd, "wifi_scan") == 0) {
        if (onWiFiScan) {
            String result = onWiFiScan();
            sendResponse(cmd, true, result.c_str());
        }
    } else if (strcmp(cmd, "device_info") == 0) {
        String info = onGetDeviceInfo ? onGetDeviceInfo() : "{}";
        sendResponse(cmd, true, info.c_str());

    } else {
        sendResponse(cmd, false, "{\"msg\":\"unknown cmd\"}");
    }
}
