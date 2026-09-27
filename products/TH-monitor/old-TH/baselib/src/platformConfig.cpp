#include "platformConfig.hpp"

#include "debug.hpp"

void PlatformConfig::Gsm::dump() {
  PRINT(apn);
}

static void printAPs(char* aps, const size_t len) {
  for (char*tmp=aps; tmp < (aps + len); ++tmp) {
#define ISNUL(ptr) (*(ptr) == '\0')
    if (ISNUL(tmp)) {
      if (tmp-aps > 2 && ISNUL(tmp-1) && ISNUL(tmp-2)) { // ssid/pwd blank
        break;
      }
      PRINT(',');
    } else {
      PRINT(*tmp);
    }
#undef ISNUL
  }
}

void PlatformConfig::WiFi::dump() {
  PRINT("Portal{", portal.enabled);
  if (portal.enabled) {
    PRINT(",", portal.timeout, portal.frequency);
  }
  PRINT("}");
  PRINT("Multi{", wiFiMulti.enabled);
  if (wiFiMulti.enabled) {
    PRINT("{"); printAPs(wiFiMulti.aps, wiFiMulti.len); PRINT("}");
  }
  PRINT("}");
}

void PlatformConfig::MQTT::dump() {
  PRINT("{", stage, "}");
}

void PlatformConfig::dump() {
  PRINT("Platform(");
  PRINT("Gsm{"); gsm.dump(); PRINT("}");
  PRINT(",WiFi{"); wiFi.dump(); PRINT("}");
  PRINT(",MQTT{"); mqtt.dump(); PRINT("}");
  PRINT(deviceId);
  PRINT(")");
}
