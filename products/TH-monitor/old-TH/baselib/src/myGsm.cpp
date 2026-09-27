#include "myGsm.hpp"

#include "platformConfig.hpp"

#include <HardwareSerial.h>

#define DEBUG
#include "debug.hpp"

MyGsm::MyGsm():
  buildConfig(PlatformConfig::buildTime.gsm)
  , serialAT(Serial1)
#ifdef DUMP_AT_COMMANDS
  , debugger(serialAT, Console), modem(debugger)
#else
  , modem(serialAT)
#endif
  , cl(modem)
  , gprs{"", "", ""}
{
}

void MyGsm::powerup()
{
  digitalWrite(buildConfig.pwkeyPin, LOW); // Power ON through ULN
  delay(2000);
  digitalWrite(buildConfig.rstPin, HIGH);  // Reset through  ULN
  delay(2000);
  digitalWrite(buildConfig.powerOnPin, HIGH);  // Release Reset

  TRACE(F("Powerup done"));
}

static const int MAX_INIT_DELAY=20; // seconds
bool MyGsm::initModem() {
  TRACE("Initializing modem...");

  int count = 0;
    for (int i = MAX_INIT_DELAY; ; --i)
    {
      PRINT(i, " ");

      if (count <= 0 || i <= 0) {
#if 0
        if (modem.init()) {
#else
        if (modem.restart()) {
#endif
          PRINT("successful", _NL);
          return true;
        }
      }

      if (++count >= 5) // every 5 seconds
        count = 0;

      if (i <= 0)
        break;

      delay(1000);
    }

    PRINT("failed", _NL);
    return false;
}

bool MyGsm::waitForNetwork() {
  TRACE("WaitforNetwork");
  bool rc = modem.waitForNetwork(4L*60*1000);
  TRACE("WaitforNetwork=", rc);
  if (!rc) {
    WARN("WaitforNetwork failed");
  }

#if 0
  // CHECKME: ESP32 compilation error
  int count = modem.countSMS();
  if (count < 0) {
    TRACE("SMScount", count);
    rc = false;
  }
#endif

  return rc;
}

const char* MyGsm::setupModem() {
    if (!initModem()) {
      return "initializing modem failed";
    }

    String modemInfo = modem.getModemInfo();
    TRACE("Modem:", modemInfo);

    if (!checkSim()) {
      return "SIM not ready";
    }

    return nullptr;
}

bool MyGsm::checkNetwork()
{
#if TINY_GSM_USE_WIFI
  TRACE(F("Setting SSID/password..."));
  if (!modem.networkConnect(wifiSSID, wifiPass)) {
    WARN("Failed to set SSID/Password", wifiSSID, wifiPass);
    return("Failed to set SSID/password");
  }
  PRINTLN(" OK");
#endif

    static const int MAX = 3;
    for (int i=0; i<MAX; ++i) {
      bool rc = waitForNetwork();
      if (rc) {
        TRACE("waitForNetwork(#", i, ") succeeded");
        return true;
      }

      delay(1000);
    }

    WARN("waitForNetwork failed; #tries=", MAX);
    return false;
}

bool MyGsm::setup(Config& config) {
  gprs.apn = config.apn;
  TRACE(F("MyGsm::setup"), gprs.apn);

  pinMode(buildConfig.pwkeyPin, OUTPUT);
  pinMode(buildConfig.rstPin, OUTPUT);
  pinMode(buildConfig.powerOnPin, OUTPUT);

  // TinyGsmAutoBaud(SerialAT);
  delay(3000);

  powerup();
  delay(3000);

#ifdef ESP8266
  Serial1.begin(115200, SERIAL_8N1, /* buildConfig.rxPin, */ SERIAL_FULL, buildConfig.txPin, false);
#elif ESP32
  Serial1.begin(115200, SERIAL_8N1, buildConfig.rxPin, buildConfig.txPin, false);
#endif
  delay(3000);

  auto rc = setupModem();
  if (rc != nullptr) {
    ERROR("Setting up GSM Modem:", rc);
    return false;
  }

  if (checkNetwork()) {
    dumpInfo();
  }

  return true;
}

static const int SMS_TRIALS = 3;
bool MyGsm::sendSMS(const String& toPhone, const String& msg) {
  for (int i=0; i<SMS_TRIALS; ++i) {
    bool res = modem.sendSMS(toPhone, msg );
    TRACE("Send SMS Trial#", (i+1), toPhone, msg, res? "OK": "Fail");
    if (res)
      return true;
  }
  WARN(F("Send SMS failed after"), SMS_TRIALS, F("trials"), toPhone, msg);
  return false;
}

bool MyGsm::sendSMS(const String& toPhone, const char* msg) {
  String smsg(msg);
  return sendSMS(toPhone, smsg);
}

bool MyGsm::recvSMS(String& fromPhone, String& msg) {
#if 0
  int count = modem.countSMS();
#else
  int count = -1; // CHECKME: ESP32 compilation error
#endif
  TRACE("SMS Count", count);

  if (count <= 0)
    return false;

#if 0
  bool res = modem.readSMS(count, msg, fromPhone);
#else
  bool res = false; // CHECKME: ESP32 compilation error
#endif
  TRACE("SMS read", fromPhone, msg, res);
  
  fromPhone.trim();
  msg.trim();

  return true;
}

void MyGsm::dumpInfo()
{
  TRACE(F("Modem"), modem.getModemInfo());

  if (!checkSim()) {
    return;
  }
  TRACE(F("SIM CCID"), modem.getSimCCID());
  TRACE(F("IMEI"), modem.getIMEI());

  auto rc = modem.isNetworkConnected();
  TRACE(F("Network connected"), rc);
  if (rc) {
    TRACE(F("Operator"), modem.getOperator());
    TRACE(F("Signal quality"), modem.getSignalQuality());

#ifdef TINY_GSM_DEBUG
    //CHECKME: Doesnt seem to work; so print only if GSM_DEBUG
    TRACE(F("Balance (USSD)"), modem.sendUSSD("*111*2#"));
    TRACE(F("Phone number (USSD)"), modem.sendUSSD("*161#"));
#endif
  }
}

bool MyGsm::connect() {
  static const int TRIALS=3;
  for (int i=0; i<TRIALS; ++i) {
    TRACE(F("GPRS:: connecting to"), gprs.apn, gprs.user, gprs.password);
#if 1
    if (modem.gprsConnect(gprs.apn.c_str(), gprs.user.c_str(), gprs.password.c_str())) {
#else
    if (modem.gprsConnect("www", "", "")) { // remove hardcode once stable
#endif
      TRACE("Success attempt#", (i+1));
      break;
    }
    delay(1000);
  }

  auto rc = modem.isGprsConnected();
  TRACE("GPRS::Connect::Result", rc);

  return rc;

#if 0
  modem.gprsDisconnect();
  TRACE("Disconnected");
#endif
}

bool MyGsm::connected() {
  auto rc = modem.isGprsConnected();
  // TRACE("GPRSConnected", rc);
  return rc;
}

void MyGsm::loop() {
  modem.maintain();
}

IPAddress MyGsm::localIP() {
  return modem.localIP();
}

bool MyGsm::simUnlock(const char* pin) {
  auto status = modem.getSimStatus();
  switch(status) {
    case SIM_LOCKED:
      break;
    default:
      TRACE("Gsm::Unlock: didnt; status was", status);
      return true;
  }

  bool rc = modem.simUnlock(pin);
  TRACE("Gsm::simUnlock", pin, rc);
  return rc;
}

bool MyGsm::checkSim() {
  auto status = modem.getSimStatus();
  TRACE("Gsm::SimStatus", status);
  return status == SIM_READY;
}
