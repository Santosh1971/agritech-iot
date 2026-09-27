#ifdef ESP8266
#elif ESP32
#include "GsmOTAManager.hpp"

#include <Update.h>

#include "miscUtils.hpp"

#define DEBUG
#include "debug.hpp"

GsmOTAManager::GsmOTAManager(MyGsm& myGsm, const char* otaUrl, const char* filename):
  OTAManager(otaUrl, filename), myGsm(myGsm) {
}

void GsmOTAManager::setup() {
}

#include "platform.hpp"
extern Platform platform;

// Simply continuing without restart seems unreliable
#if 0
#define FATAL_EXIT { \
  PRINTLN("...Aborting the update..."); \
  return; \
}
#else
#define FATAL_EXIT { \
  PRINTLN("=== Restarting === ..."); \
  platform.reboot("Aborted upgrade"); \
  return; \
}
#endif
static auto FATAL = F("- FATAL -: Aborting the Update:");
#define DEBUG_PRINT(...) { TRACE(millis(), "- ", __VA_ARGS__); }
#define DEBUG_FATAL(...) { \
  PRINTLN(millis(), FATAL, "- ", __VA_ARGS__); \
  FATAL_EXIT; \
}

#include "task.hpp"

void GsmOTAManager::start(const char* filename, Messenger messenger)
{
  if (messenger == nullptr)
    messenger = [](String key, String val) {
      TRACE("Alert", key, val);
    };

  String fotaUrl = buildFotaUrl(filename);
  TRACE("GsmOTA::start", fotaUrl);
  messenger("FOTA Started", fotaUrl);

  String protocol, host, url;
  int port;

  if (!MiscUtils::parseURL(fotaUrl, protocol, host, port, url)) {
    DEBUG_FATAL(F("Cannot parse URL"), fotaUrl);
  }

  DEBUG_PRINT(String("Connecting to ") + host + ":" + port);

  auto& modem = myGsm._modem();

  Client* client = NULL;
  if (protocol == "http") {
    client = new TinyGsmClient(modem);
    if (!client->connect(host.c_str(), port)) {
      messenger("Error", "Cannot connect to host");
      DEBUG_FATAL(F("Client not connected"));
    }
  } else if (protocol == "https") {
    client = new TinyGsmClientSecure(modem);
    if (!client->connect(host.c_str(), port)) {
      messenger("Error", "Cannot connect to host");
      DEBUG_FATAL(F("Client not connected"));
    }
  } else {
    messenger("Error", "Unsupported protocol");
    DEBUG_FATAL(String("Unsupported protocol: ") + protocol);
  }
  
  DEBUG_PRINT(String("Requesting ") + url);

  client->print(String("GET ") + url + " HTTP/1.0\r\n"
               + "Host: " + host + "\r\n"
               + "Connection: keep-alive\r\n"
               + "\r\n");

  long timeout = millis();
  while (client->connected() && !client->available()) {
    if (millis() - timeout > 10000L) {
      messenger("Error", "Response timeout");
      DEBUG_FATAL("Response timeout");
    }
  }

  // Collect headers
  String md5;
  int contentLength = 0;

  while (client->available()) {
    String line = client->readStringUntil('\n');
    line.trim();
    //Console.println(line);    // Uncomment this to show response headers
    line.toLowerCase();
    if (line.startsWith("content-length:")) {
      contentLength = line.substring(line.lastIndexOf(':') + 1).toInt();
    } else if (line.startsWith("x-md5:")) {
      md5 = line.substring(line.lastIndexOf(':') + 1);
    } else if (line.length() == 0) {
      break;
    }
  }

  if (contentLength <= 0) {
    messenger("Error", "Content-Length not defined");
    DEBUG_FATAL("Content-Length not defined");
  }

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Update.printError(Console);
    messenger("Error", "OTA begin failed");
    DEBUG_FATAL("OTA begin failed");
  }

  if (md5.length()) {
    DEBUG_PRINT(String("Expected MD5: ") + md5);
    if(!Update.setMD5(md5.c_str())) {
      messenger("Error", "Cannot set MD5");
      DEBUG_FATAL("Cannot set MD5");
    }
  }

  DEBUG_PRINT("Flashing", contentLength, "bytes...");

  // The next loop does approx. the same thing as Update.writeStream(http) or Update.write(http)

  int written = 0;
  int progress = 0;
  uint8_t buff[TINY_GSM_RX_BUFFER];
  while (written < contentLength) {
    int len = client->read(buff, sizeof(buff));
    // TRACE("read bytes", len, written);
    if (len > 0) {
      auto wn = Update.write(buff, len);
      if (wn != len)
        WARN("written different", wn, len);

      written += len;
#if 1
      // PRINT(".");
      int newProgress = (written*100)/contentLength;
      if (newProgress - progress >= 5 || newProgress == 100) {
        progress = newProgress;
        //String p = String(written) + "(" + progress + "%)";
        PRINT("", progress + String("%"));
      }
#endif

      timeout = millis();
      continue;
    }

    if (client->connected()) {
      delay(1);
      if (millis() - timeout > 10000L) {
        messenger("Error", "Timeout on download/flash");
        DEBUG_FATAL("Timeout");
      }
      continue;
    }

    if (!client->available()) {
      break;
    }
  }
  PRINTLN();
  DEBUG_PRINT("... flashed", written, "bytes");

  if (written != contentLength) {
    Update.printError(Console);
    messenger("Error", String("Write failed. Written ") + written + "differ from + content length " + contentLength);
    DEBUG_FATAL(F("Write failed. Written "), written, "/", contentLength, "bytes", "available", client->available(), "connected", client->connected());
  }

  if (!Update.end()) {
    Update.printError(Console);
    messenger("Error", F("Update not ended"));
    DEBUG_FATAL(F("Update not ended"));
  }

  if (!Update.isFinished()) {
    messenger("Error", F("Update not finished"));
    DEBUG_FATAL(F("Update not finished"));
  }

  messenger("Upgrade Success", "Succeeded");

  platform.reboot("SystemUpgrade");
}
#undef FATAL_EXIT
#undef DEBUG_PRINT
#undef DEBUG_FATAL

void GsmOTAManager::loop() {
  // We do nothing here at present
  // start() is blocking
}
#endif
