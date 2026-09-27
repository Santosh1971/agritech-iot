#pragma once

#include <Arduino.h>

struct PlatformConfig {
  struct Gsm {
    char apn[21];

    void dump();
  } gsm;

  struct WiFi {
    struct Portal {
      bool enabled;
      unsigned long timeout;  // in seconds when became active
      unsigned long frequency;  // in seconds; how long it can take to become active again, since it will block
    } portal;
    struct WiFiMulti {
      bool enabled;
      static const size_t len = 200;
      char aps[len];  // concatenated, '\0' delimited, ssid/passwords
    } wiFiMulti;
    void dump();
  } wiFi;

  struct MQTT {
    enum Stage { Dev=0, Test=1, Prod=2, NA=-1 } stage;
    void dump();
  } mqtt;

  char deviceId[21];  // logical device id of upto 20 chars; defaults to macId

  void dump();

  /**
   * Build time configuration
   * Primarily pin mappings for now
   */
  static struct BuildTime {
    struct Gsm {
      const int rstPin;
      const int pwkeyPin;
      const int powerOnPin;
      const int txPin;
      const int rxPin;
    } gsm;

    struct { 
      const int sdaPin, sclPin;
    } misc;

    struct {
      const char* broker;
      const int port;
    } mqtt;
  } buildTime;
};
