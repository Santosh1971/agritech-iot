#include "WiFiOTAManager.hpp"

#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#elif ESP32
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#else
  #error("Unsupported platform: neither ESP8266 nor ESP32")
#endif

#include "platform.hpp"

#define DEBUG
#include "debug.hpp"

WiFiOTAManager::WiFiOTAManager(MyWiFiManager& myWiFiManager, const char* otaUrl, const char* filename):
  OTAManager(otaUrl, filename), myWiFiManager(myWiFiManager) {
}

void WiFiOTAManager::setup() {
  TRACE(F("FOTAFilelocation:"), buildFotaUrl());
}

void WiFiOTAManager::loop(void) {
  // do nothing
}

void WiFiOTAManager::start(const char* filename, WiFiOTAManager::Messenger messenger) {
  Messenger send = [=](String key, String val) {
    TRACE("WiFiOTA", key, val);
    if (messenger != nullptr) {
      messenger(key, val);
    }
  };

  String FOTAFilelocation = buildFotaUrl(filename);

  // wait for WiFi connection
  if (myWiFiManager.connect(false)) {  // TODO: move this check to caller
    send("FOTA started", FOTAFilelocation);

#ifdef ESP8266
    auto& httpUpdate = ESPhttpUpdate;
#elif ESP32
    // httpUpdate already set
#endif
    t_httpUpdate_return ret = httpUpdate.update(myWiFiManager.wiFiClient(), FOTAFilelocation);

    PRINTLN(F("Finish Update"), ret);

    // imp   : Basically ESP.restart() doesn't work correctly if the board was not reset after serial upload.
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        ERROR("HTTP_UPDATE_FAILED:", httpUpdate.getLastError(), httpUpdate.getLastErrorString());
        send("FOTA Error", httpUpdate.getLastError() + String(" : ") +         
                  httpUpdate.getLastErrorString());
        
        break;

      case HTTP_UPDATE_NO_UPDATES:
        WARN(F("HTTP_UPDATE_NO_UPDATES"));
        send("FOTA WARNING", "HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        send("FOTA OK", "Succeeded");

        // FIXME: call platform.reboot() here
        break;
    }
  }
}
