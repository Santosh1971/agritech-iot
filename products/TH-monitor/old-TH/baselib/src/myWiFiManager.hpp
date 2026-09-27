#pragma once

#include <Arduino.h>

#include <WiFiClient.h>

#include "platformConfig.hpp"
#include "millisTimer.hpp"
#include "myWiFiAPs.hpp"

class MyWiFiManager {
    WiFiClient cl;

    String portalAPname;  // suggested AP name

    MillisTimer portalTimer;  // reduce frequency of creation of portal

#if ESP32
    bool persistent = true; // need to track separately
#endif
    bool getPersistent();
    void setPersistent(bool persistent);

  public:
    using Config=PlatformConfig::WiFi;

    /**
     * Callback on config change: to commit to EEPROM
     */
    using OnConfigChange = MyWiFiAPs::OnChange;
    MyWiFiManager(String portalAPname);

    // auto-connect callbacks; used in indicator
    enum AutoConnectEvent {
      Entered = 'E',
      Failed = 'F', // exit with failure
      Succeeded = 'S',  // exit with success
    };
    using OnAutoConnect = std::function<void (AutoConnectEvent)>;
    void setup(Config& config, OnConfigChange onConfigChange,
        OnAutoConnect onAutoConnect=nullptr);

    /*
     * if already connected returns true
     * tries to connect by saved default SSID, then wifi-multi (if enabled)
     * if @autoCon, then invokes autoConnect if others dont succeed
     * @return true if any succeeded; false otherwise
     */
    bool connect(bool autoCon=false);
    bool connected();

    /**
     * Connect to a specific access point
     * Fails if couldnt connect as specified
     * @return ""; errmsg if failed
     */
    String connect(String ssid, String passwd);
    void reset(); // disconnect and reset saved ssid

    void loop();

    String id();

    WiFiClient& wiFiClient();

    IPAddress localIP();

    MyWiFiAPs* getAPsHelper();  // used to manage saved APs

    bool multiConnect();  // explicitly used in OTA

  private:
    bool autoConnect(OnAutoConnect); // using WiFiManager.autoConnect

    /*
     * save a valid ssid/pwd into configuration on successful connect
     * @return true if saved; false if already there, or save disabled
     */
    bool saveToConfig(String ssid, String pwd, MyWiFiAPs::Option option);
    bool saveToConfig();  // the currently connected SSID as the top one
    bool retrieveFromConfig(String& ssid, String& pwd);  // the saved currently connected SSID

    Config* config;
    MyWiFiAPs* myWiFiAPs; // use unique_ptr
    OnAutoConnect onAutoConnect;
};
