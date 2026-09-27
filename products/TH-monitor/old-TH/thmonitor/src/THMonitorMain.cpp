#include "globals.h"
#include "hardware.h"
#include "miscUtils.hpp"
#include "platformConfig.hpp"

PlatformConfig::BuildTime PlatformConfig::buildTime{
    {MODEM_RST, MODEM_PWKEY, MODEM_POWER_ON, MODEM_TX, MODEM_RX},
    {I2C_SDA, I2C_SCL},
    {
#ifdef MQTT_BROKER
        MQTT_BROKER
#else
        "mqtt.agrisensorsandcontrols.com"
#endif
        ,
#ifdef MQTT_PORT
        MQTT_PORT
#else
        1883
#endif
    },
};

#include "LEDController.hpp"
static LEDController wifiIndicator(LED_PIN_BLUE);  // connected to GPIO1 pin

#include "platform.hpp"
Platform platform(MiscUtils::formMAC(), {__FILE__, __DATE__, __TIME__}, &wifiIndicator);

#define DEBUG
#include "debug.hpp"

Stream &Console(Serial);

#include "MQTTManager.hpp"
#include "MQTTMessenger.hpp"
#include "globals.h"
#include "miscUtils.hpp"

const char *STORE = "/data/TH.log";
#include "THStore.hpp"
THStore thStore(STORE);

#include "THSender.hpp"

THSender thSender(thStore);

bool setupEEPROMStorage() {
  // setup values area
  auto &values = EEPROMstorage.values();

  values.TempInterval = 60;  // every minute
  values.THstatus = {0, 0};  // seqNum, lastTs
  values.THstore = {thStore.maxLimit() * 0.8};

  bool gotValues = EEPROMstorage.setup();

#ifdef DEBUG
  Serial.print("EEPROM setup:");
  values.dump();
  Serial.println();
#endif

  return gotValues;
}

static void saveStoreState(THStore::State &newState, bool onlyIxChanged) {
  auto &target = EEPROMstorage.values().THstore;

  if (onlyIxChanged) {  // most frequent occurrence
    target.indexes = newState.indexes;
    EEPROMstorage.save(target.indexes);
  } else {
    target = newState;
    EEPROMstorage.save(target);
  }
}

#include "baseConsoleCommander.hpp"
BaseConsoleCommander consoleCommander;

void myReboot() {
  TRACE("Rebooting...");
  EEPROMstorage.save(EEPROMstorage.values().THstatus);
  delay(5000);
  ESP.reset();
  delay(5000);
}

#include "EEPROMStorage.hpp"
#include "myTimer.hpp"
#include "task.hpp"
#include "topicMgr.hpp"

#ifndef PRODUCT_TYPE
#error("PRODUCT_TYPE is not defined")
#endif

static class : public TopicMgr {
 public:
  bool formTopics(bool isRx, std::vector<String> &topics) {
    auto &config = platformConfigstorage.values();
    return TopicMgr::formTopics(isRx, topics, PRODUCT_TYPE, config.deviceId, (int)config.mqtt.stage);
  }
} topicMgr;

#include "MQTTMessenger.hpp"
#include "MQTTProcessor.hpp"
MQTTMessenger mqttMessenger;

#include "myDHT.hpp"
MyDHT myDHT(DHT22_PIN);

/********************************************************
   Sketch setup() function: Executes 1 time
   Function: setup(void)

   Parameter    Description
   ---------    ---------------------------------------
   return       no return value
 ********************************************************/
void setup() {
  Serial.begin(MONITOR_SPEED);  // Initialize serial port

  TRACE("\nTHMonitor");

  EEPROMstorage.begin(platformConfigstorage.size() + EEPROMstorage.size());

  platform.setup(topicMgr,
                 Platform::makeCallback<MQTTProcessor, MQTTMessenger>(mqttMessenger, topicMgr));

  platform.onReboot = [](bool save) {
    TRACE("onReboot", save);
    if (save) {
      TRACE("Saving seqNum on reboot", EEPROMstorage.values().THstatus.seqNum);
      EEPROMstorage.save(EEPROMstorage.values().THstatus.seqNum);
    }
  };

  bool refreshed = setupEEPROMStorage();
  auto &values = EEPROMstorage.values();
  if (values.THstatus.seqNum > 0) {  // value saved on reboot; let next reboot have 0
    auto seqNum = values.THstatus.seqNum;
    values.THstatus.seqNum = 0;
    EEPROMstorage.save(values.THstatus.seqNum);
    values.THstatus.seqNum = seqNum;
  }
  TRACE("Resuming seqNum at", values.THstatus.seqNum);

  Serial.println("Temp&Humi Interval:" + String(EEPROMstorage.values().TempInterval) + "(seconds)");

  myDHT.setup();

  thStore.setup(saveStoreState, refreshed ? &values.THstore : nullptr);

  platform.setOnStateChange([](Platform::State state) {
    if (state == Platform::State::NetNServer) {
      thSender.sendFromStore();
    }
  });

  // setup is done; now start regular stuff
  // CAUTION: this can be blocking
  auto res = platform.connect();
  if (res != nullptr) {
    WARN(F("Platform connection"), res);
  }

  mqttMessenger.sendSystemInfo();

  thSender.sendFromStore();  // clear any accrued rows from store

  auto startReading = [&]() {
    thSender.readSendTH();  // read send TH once

    thSender.setReadCycle(values.TempInterval);
  };
  auto now = CalendarUtils::now();
  auto nextTs = now;
  if (values.THstatus.lastTs > 0)
    nextTs = CalendarUtils::add(values.THstatus.lastTs, values.TempInterval);

  TRACE("Read cycle at", CalendarUtils::toString(now), "=@",
        CalendarUtils::toString(nextTs), "delayed?", (nextTs > now));
  if (nextTs > now) {
    timers.at(nextTs, startReading);
  } else {
    startReading();
  }
}

/********************************************************
   Sketch loop() function: Executes repeatedly
   Function: loop(void)

   Parameter    Description
   ---------    ---------------------------------------
   return       no return value
 ********************************************************/
// int startupflag=0;
// Receive data from mqtt call back... in Background
void loop() {
  platform.loop();

  timers.loop();  // all timed activities are under Timer task. initiate that

  taskManager.loop();  // tasks after timer tasks for better timeliness

  consoleCommander.loop();
}
