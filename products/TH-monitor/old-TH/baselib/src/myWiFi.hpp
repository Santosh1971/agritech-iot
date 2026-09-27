#pragma once

#include <Arduino.h>

#include <WiFiClient.h>

#include "myClient.hpp"
#include "myWiFiManager.hpp"

/**
 * A wrapper class to adapt MyWiFiManager to MQTTManager
 */
class MyWiFi: public MyClient {
    MyWiFiManager& myWiFiManager;  // reduce frequency of creation of portal
    bool firstTime = true; // used in connect

  public:
    MyWiFi(MyWiFiManager& myWiFiManager);

    bool connect();
    bool connected();

    void loop();

    IPAddress localIP();

    Client& client();
};
