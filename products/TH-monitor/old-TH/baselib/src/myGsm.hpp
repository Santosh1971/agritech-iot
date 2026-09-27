#pragma once

#include "myClient.hpp"
#include "platformConfig.hpp"

#include <Stream.h>

extern Stream& Console; // Used when: TINY_GSM_DEBUG => Console

/**
 * Buffer size considerations
 * 1024: Suggested by the OTA example; but during OTA, client frequently
 * disconnects before all bytes are received
 * 256: a good compromise; better speed and OTA is working
 */
#ifndef TINY_GSM_RX_BUFFER
#define TINY_GSM_RX_BUFFER 256    // 64 increased to 256
// #define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb for OTA
#endif

#define TINY_GSM_MODEM_SIM800

#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
#endif

class MyGsm: public MyClient {
  PlatformConfig::BuildTime::Gsm& buildConfig;

  Stream& serialAT;  // Serial used to stream AT commands
#ifdef DUMP_AT_COMMANDS
  StreamDebugger debugger;  // Intervening stream for dumping commands
#endif

  TinyGsm modem;

  TinyGsmClient cl;

  // GPRS
  struct {
    String apn;
    String user;
    String password;
  } gprs;

  public:
    using Config = PlatformConfig::Gsm;

    MyGsm();

    bool setup(Config& config);

    /**
     * @return true successful or if already unlocked; false if failed
     */
    bool simUnlock(const char* pin);

    bool connect();
    bool connected();

    void loop();

    IPAddress localIP();

    Client& client() { return cl; }

    bool sendSMS(const String& toPhone, const String& msg);
    bool sendSMS(const String& toPhone, const char* msg);
    bool recvSMS(String& fromPhone, String& msg);

    void dumpInfo();

    TinyGsm& _modem() {
      return modem;
    }
    bool checkSim();

  private:
    void powerup();
    bool initModem();
    bool waitForNetwork();
    const char* setupModem();
    bool checkNetwork();
};
