#include "myWiFiManager.hpp"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>          //http server library
#include <WiFiMulti.h>
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif

#ifdef FEATURE_WIFIMULTI
#ifdef ESP8266
#include <ESP8266WiFiMulti.h>
using WiFiMulti = ESP8266WiFiMulti; // alias to a common name
#elif ESP32
#include <WiFiMulti.h>
#endif
#endif

#ifdef FEATURE_WIFI_MANAGER
#include <WiFiManager.h>
#endif

#define DEBUG
#include "debug.hpp"

MyWiFiManager::MyWiFiManager(String portalAPname): portalAPname(portalAPname), portalTimer(1000*60*30) {
}

#ifdef FEATURE_WIFI_MANAGER
//gets called when WiFiManager enters configuration mode
static void configModeCallback (WiFiManager *myWiFiManager) {
  PRINTLN(F("Entered config mode"));
  PRINTLN(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  PRINTLN(myWiFiManager->getConfigPortalSSID());
}
#endif


void MyWiFiManager::setup(Config& config,
    MyWiFiManager::OnConfigChange onConfigChange,
    MyWiFiManager::OnAutoConnect onAutoConnect)
{
  this->config = &config;
  this->onAutoConnect = onAutoConnect;

  auto& multi = config.wiFiMulti;
  if (multi.enabled) { // add multi via portal too
    myWiFiAPs = new MyWiFiAPs(multi.aps, multi.len, onConfigChange);
    if (myWiFiAPs == nullptr || myWiFiAPs->count() <= 0) {
      WARN("No SSID set in APs list");
    }
  }

  if (config.portal.enabled)
    portalTimer = MillisTimer(config.portal.frequency*1000);

  auto rc = WiFi.begin();
  TRACE("MyWiFiManager::setup", rc, WiFi.SSID());
}

bool MyWiFiManager::connect(bool autoCon) {
  if (connected())
    return true;

  WiFi.begin(); // without this, SSID seems to get reset

  if (WiFi.SSID() != "") {
#ifdef DEBUG
    PRINT(F("MyWiFiManager::"), WiFi.SSID());
#endif
    
    for (auto i=0; i<20; ++i) {
      auto st = WiFi.status();

      if (st == WL_CONNECTED) {
        TRACE(F("Connected"));
        return true;
      }
#ifdef DEBUG
      PRINT("", st);
#endif
      delay(500);
    }
  }
  PRINT(F("X"));

  bool rc;
#if 0
  // doesnt work; call connect("", "")?
  rc = WiFi.reconnect();
  TRACE("reconnect::res", rc, connected());
  if (rc) {
    return true;
  }
#endif

#if 0
  { // doesnt work either
    auto res = connect(String(""), String("")); // use saved ones
    if (res == "") {
      return true;
    }
  }
#endif

  if (myWiFiAPs != nullptr) {
    rc = multiConnect();
    if (rc) {
      return true;
    }
  }

  if (autoCon) {
    rc = autoConnect(this->onAutoConnect);
    // TRACE("autoConnect::res", rc);
    if (!rc) {
      return false;
    }
  }

  rc = connected();
  // TRACE("Connect::status", rc);
  return rc;
}

String MyWiFiManager::connect(String ssid, String passwd) {
  TRACE("WiFiStatus=", WiFi.status(), "Disconnecting first");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  //onlineIndicator.refresh();

  TRACE("Connecting", ssid, WiFi.status(), "...");

  auto persistent = getPersistent();
  setPersistent(true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), passwd == ""? NULL: passwd.c_str());

  // #Attempts seemed to vary from 6 to 16; putting 30 for safety
  for (int i=0; i<30; ++i) {
    auto st = WiFi.status();

    if (st == WL_CONNECTED) {
#ifdef DEBUG
      PRINTLN(" Connected", persistent);
#endif
      portalTimer.stop(); // prevent autoconfig
      setPersistent(persistent);  // reset
      return String("");
    }
#ifdef DEBUG
    PRINT("", st);
#endif
    delay(500);
  }

  WARN("Failed", WiFi.status());
  setPersistent(persistent);  // reset
  return String("Failed:") + WiFi.status();

  //onlineIndicator.refresh();
}

void MyWiFiManager::reset() {
  auto persistent = getPersistent();
  setPersistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false /*, true*/);
  setPersistent(persistent);  // reset
  TRACE("myWifiManager::reset", persistent, WiFi.SSID());
}

bool MyWiFiManager::connected() {
  return (WiFi.status() == WL_CONNECTED);
}

void MyWiFiManager::loop() {
  // nothing needed
}

bool MyWiFiManager::autoConnect(OnAutoConnect onAutoConnect) {
#ifdef FEATURE_WIFI_MANAGER
  if (!config->portal.enabled)
    return false;

  if (portalTimer.running()) {
    // TRACE("Config portal ran ", portalTimer.counter(), "m-secs ago; skipping repeated autoconfig");
    return false;
  }
  portalTimer.start();

  TRACE("Auto-configuring WiFi", "timeout in", config->portal.timeout, "seconds");

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name SSID_AP
    //and goes into a blocking loop awaiting configuration

//          // Give the ESP some time to connect
//          delay(3 * 1000);
// if (WifiConnection::isConnected()) {    
  WiFiManager wifiManager;

   // wifiManager.setConnectTimeout(5); // timeout to connect the existing SSID
  wifiManager.setConfigPortalTimeout(config->portal.timeout);
    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setDebugOutput(false);

  if (onAutoConnect == nullptr)
    onAutoConnect = [](AutoConnectEvent e) { };

  onAutoConnect(AutoConnectEvent::Entered);
  bool rc = wifiManager.autoConnect(portalAPname.c_str());
  if (rc) {
    TRACE("Autoconnect to", WiFi.SSID(), "succeeded");
    saveToConfig();
    onAutoConnect(AutoConnectEvent::Succeeded);
  } else {
    WARN("Auto-configuration failed");
    onAutoConnect(AutoConnectEvent::Failed);
    String ssid, pwd;
    if (retrieveFromConfig(ssid, pwd)) {
      TRACE("Reconnecting to saved", ssid);
      connect(ssid, pwd);
    }
  }

  return rc;
#else
  return false;
#endif
}

bool MyWiFiManager::multiConnect() {
#ifdef FEATURE_WIFIMULTI
  WiFi.disconnect(true);  // seems required to force rediscovery of APs

  WiFi.mode(WIFI_STA);
  delay(10);

  WiFiMulti wiFiMulti;

  int count = 0;
  myWiFiAPs->find([&](String& ssid, String& pwd) {
    ++count;

    TRACE("Add AP", ssid, pwd);

    wiFiMulti.addAP(ssid.c_str(), pwd.c_str()); // CAUTION: special character doesnt work with multiwiFi.
    return false; // keep looking
  });

  if (count == 0) {
    // WARN("No SSID set in APs list", count);
    return false;
  }

#ifdef ESP8266
  WiFi.scanNetworks();  // ESP8266 requires scan to have started before calling run
#endif
  TRACE("Connecting to WiFiMulti...", count);
  auto rc = wiFiMulti.run();
  TRACE("res", rc);
  if (rc == WL_CONNECTED) {
    TRACE("WiFi connected:", WiFi.SSID(), "IP address:", WiFi.localIP());
    // CHECKME: move this SSID to the front of the list
    saveToConfig();
  } else {
    WARN("WiFiMulti failed", rc);
  }

  return (rc == WL_CONNECTED);
#else
  return connected();
#endif
}

bool MyWiFiManager::saveToConfig(String ssid, String pwd, MyWiFiAPs::Option option) {
  if (myWiFiAPs != nullptr)
    return myWiFiAPs->add(ssid, pwd, option);
  return false;
}
bool MyWiFiManager::saveToConfig() {
  return saveToConfig(WiFi.SSID(), WiFi.psk(), MyWiFiAPs::Option::Top);
}

bool MyWiFiManager::retrieveFromConfig(String& ssid, String& pwd) {
  if (myWiFiAPs == nullptr)
    return false;

  return myWiFiAPs->find([&](String& ssid2, String& pwd2) {
      ssid = ssid2;
      pwd = pwd2;
      return true;  // first one 
  });
}

String MyWiFiManager::id() {
  return WiFi.SSID();
}

IPAddress MyWiFiManager::localIP() {
  return WiFi.localIP();
}

WiFiClient& MyWiFiManager::wiFiClient() {
  return cl;
}

MyWiFiAPs* MyWiFiManager::getAPsHelper() {
  return myWiFiAPs;
}

bool MyWiFiManager::getPersistent() {
#if ESP32
  return persistent;  // no way to get this for ESP32; so return local copy
#else
  return WiFi.getPersistent();
#endif
}

void MyWiFiManager::setPersistent(bool persistent) {
#if ESP32
  this->persistent = persistent;  // store in a local copy before setting
#endif
  WiFi.persistent(persistent);
}
