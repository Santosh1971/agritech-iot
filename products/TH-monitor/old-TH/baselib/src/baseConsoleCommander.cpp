#include "baseConsoleCommander.hpp"

#include <Arduino.h>

#include "platform.hpp"

#include <cstring>

#include "platformConfig.hpp"

#define DEBUG
#include "debug.hpp"

#include <stdio.h>

BaseConsoleCommander::BaseConsoleCommander(): cmdSerial(Console, "CMDConsole") {
}

#if 0
static void showHelp() {
  PRINTLN(F( R"HELP(
Valid commands are:
e: Print the current EEPROM contents on console
E: Reset EEPROM marker (to 0). you may reset the device to force defaulting of contents
     Note: this will reset the topic_stage to 0(dev) too.
r: Reset the device
T0 (T1 or T2): Change the 'topic_stage' to dev/test/prod respectively. Device must be reset for this to have effect.
Q<payload>: Send Mock MQ payload to the device.
  E.g. Q{"payloadId":"SetTime","YYYY":2019,"MM":5,"DD":5,"hh":21,"mm":58,"ss":0} to send a Set time payload
P: Send Mock simple SetProgram payload
S (or S0): Send MQTT Status message for GH#0 to the platform
)HELP" ));
}
#endif

#ifndef USE_GSM
#include "WiFiFactory.hpp"
static MyWiFiManager* myWiFiManager() {
  auto pWiFiFact = static_cast<WiFiFactory*>(&platform.factory());
  return &pWiFiFact->_myWiFiManager();
}
#endif

bool BaseConsoleCommander::loop() {
  String cmd;
  if (!cmdSerial.readLine(cmd))
    return false;

  cmd.trim();
  if (cmd == "")
    return false;

  char inChar = cmd.charAt(0);
  String args = cmd.substring(1);
  args.trim();

  if (!process(inChar, args)) {
     PRINTLN("Unknown cmd:", inChar, ":skipped");
     return false;
  }

  return true;
}

bool BaseConsoleCommander::process(char inChar, String args) {
    switch(inChar) {
      case 'r': // reboot
           {
              platform.reboot("Reboot Command");
           }
        break;

      case 'T': // reset topic_stage
        {
          int t = -1;
          if (sscanf(args.c_str(), "%d", &t) != 1 || t < -1 || t > 2) {
            TRACE(F("Invalid argument for cmd:"), String(inChar));
            TRACE(F(":type 0/1/2 after it for dev/test/prod; got"), args);
            break;
          }

          using Stage=PlatformConfig::MQTT::Stage;
          auto& stage = platformConfigstorage.values().mqtt.stage;
          stage = (Stage)t;
          platformConfigstorage.save(stage);

          TRACE(F("Topic_stage is now saved as "), String(t));
          TRACE(F("Please restart to put this to effect"));
        }
        break;

#ifndef USE_GSM
      case 'W': // set live WiFi SSID and password
        {
          auto pMyWiFiMgr = myWiFiManager();
          if (pMyWiFiMgr == nullptr) {
            ERROR("Not configured for WiFi!");
            break;
          }

          args.trim();
          if (args == "") {
            pMyWiFiMgr->reset();
            break;
          }
          char ssid[21], passwd[21];
          if (sscanf(args.c_str(), "%20s %20s", ssid, passwd) < 1) {
            PRINT(F("'W <ssid> <passwd>' to reset live ssid/pwd:"), args);
            break;
          }

          auto rc = pMyWiFiMgr->connect(ssid, passwd);
          if (rc == "")
            PRINTLN("Reset ssid successfully", ssid);
          else
            WARN("Issue resetting ssid to", ssid, rc);
        }

        break;

      case 'w': // manipulate saved WiFi SSID and passwords
        args.trim();
        {
          char subcmd;
          char ssid[21], pwd[21];
          auto argc = sscanf(args.c_str(), "%c %20s %20s", &subcmd, ssid, pwd);
          std::function<void ()> usage = [&args] {
            PRINTLN(F("'w [l|a|r] <ssid> <pwd>' to manage saved ssid/pwd(s):\n"
                  "l: list; a: add ssid/pwd; r: remove ssid"), args);
          };

          if (argc < 1) {
            usage();
            break;
          }

          auto pMyWiFiMgr = myWiFiManager();
          if (pMyWiFiMgr == nullptr) {
            ERROR("Not configured for WiFi!");
            break;
          }
          auto aps = pMyWiFiMgr->getAPsHelper();
          if (aps == nullptr) {
            WARN("APs are not saved; please change settings");
            break;
          }

          switch(subcmd) {
            case 'l':
              PRINTLN("SSIDS: BEGIN>>>");
              aps->find([](String ssid, String pwd) {
                    PRINTLN(ssid);
                    return false;
                  });
              PRINTLN("SSIDS: END<<<");
              break;
            case 'a':
              if (argc != 3) {
                usage(); break;
              }
              PRINTLN("Adding", ssid, pwd);
              aps->add(ssid, pwd, aps->Option::Bottom);
              break;
            case 'r':
              if (argc != 2) {
                usage(); break;
              }
              PRINTLN("Removing", ssid);
              aps->remove(ssid);
              break;
            default:
              usage(); break;
          }
        }
        break;

#endif

      case 'e':
           PRINT(F("Platform EEPROM contents:"));
           platformConfigstorage.matchMarker();
           platformConfigstorage.values().dump();
           PRINTLN();
           break;

      case 'E':
           PRINT(F("Reset Platform EEPROM marker:"));
           platformConfigstorage.resetMarker(0);
           PRINTLN();
           break;

      case 'O':
        args.trim();
        {
          bool mode = args == "1";
          PRINTLN(F("Setting Platform::disabled to:"), mode);
          platform.setDisabled(mode);
        }
        break;

      default:
           return false;
  }

  return true;
}
