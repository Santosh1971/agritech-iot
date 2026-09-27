#pragma once

#include <Arduino.h>

#include <Client.h>

class MyClient {
  public:
    virtual Client& client()=0; // wraps around a client and provides access to it

    /**
     * setup can accept different configuration data structure
     * Hence taken out of the common abstraction
     * virtual void setup() = 0; // has a setup
     */

    virtual bool connect() = 0; // connects to it; returns if connected
    virtual bool connected() = 0; // if connected

    virtual void loop() = 0;  // should be called every loop

    virtual IPAddress localIP() = 0;  // local IP address
};
