#include "platform.hpp"
#include "miscUtils.hpp"

#ifdef USE_GSM
  #include "myGsm.hpp"

  static MyGsm myGsm;
  #include "gsmFactory.hpp"
  static GsmFactory gsmFactory(myGsm);
  static Platform::Factory& _factory(gsmFactory);
#else
  #include "myWiFiManager.hpp"
  static MyWiFiManager myWiFiManager(MiscUtils::formMAC() + "_AP");

  #include "WiFiFactory.hpp"
  static WiFiFactory wiFiFactory(myWiFiManager);
  static Platform::Factory& _factory(wiFiFactory);
#endif

#include <string>

#define DEBUG
#include "debug.hpp"

Platform::Platform(String clientId, SystemInfo systemInfo, LEDController* indicator):
  clientId(clientId)
  , systemInfo(systemInfo)

  , myClient(_factory.getClient())
  , clientSetup(false)

  , mqttManager(_factory.getClient(),
      PlatformConfig::buildTime.mqtt.broker,
      PlatformConfig::buildTime.mqtt.port)
  , indicator(indicator)
  
  , clock(&_factory.getClock())
  , otaManager(nullptr)

  , onStateChange(nullptr)
  , currentState(NotConnected)
  , disabled(false)
{
  PRINT("Platform:", clientId, systemInfo.filename, systemInfo.date, systemInfo.time);
}

Platform::Factory& Platform::factory() {
  return _factory;
}

EEPROMStorage<PlatformConfig> platformConfigstorage(PLATFORM_STORAGE_MARKER);

#ifndef WIFI_MANAGER_ENABLE
  #ifdef ESP8266
    #define WIFI_MANAGER_ENABLE true
  #elif ESP32
    #define WIFI_MANAGER_ENABLE true
  #endif
#endif

#ifndef WIFI_MULTI_ENABLE
  #define WIFI_MULTI_ENABLE true
#endif

// setup config in EEPROM
void Platform::setupConfig() {
  auto& platformConfig = platformConfigstorage.values();
  platformConfig = PlatformConfig { 
    { // GSM
#ifdef GSM_APN
      GSM_APN
#else
      "www"
#endif
    }
    , {
      { WIFI_MANAGER_ENABLE, 180, 600 },
      { WIFI_MULTI_ENABLE, "" }  // multi wifi
    } // WiFi
    , {
#ifdef MQTT_TOPIC_STAGE
      static_cast<PlatformConfig::MQTT::Stage>(MQTT_TOPIC_STAGE)
#else
      PlatformConfig::MQTT::Stage::NA
#endif
    }, // MQTT
    ""  // bad device id
  };
  {
    auto tmp = MiscUtils::formMAC();
    ::strcpy(platformConfig.deviceId, tmp.c_str());
    TRACE(F("PlatformConfig::deviceId"), platformConfig.deviceId, tmp);
  }

  auto rc = platformConfigstorage.setup();  // setup (from) EEPROM
  TRACE("Platform::Config::Setup", rc? "Refreshed": "Initialized");
  PRINT("Platform::Config:-->"); platformConfig.dump(); PRINTLN("<--");
}

String Platform::getDeviceId() {
  return platformConfigstorage.values().deviceId;
}

#ifndef OTA_URL
  #error ("OTA_URL is not defined")
#endif

#include "calendarUtils.hpp"
void Platform::setup(TopicMgr& topicMgr, Callback callback, uint8_t qos) {
  PRINTLN("Platform::setup", clientId);

  MiscUtils::setup();

  setupConfig();

  auto& config = platformConfigstorage.values();
  MiscUtils::printDeviceInfo(config.deviceId); // print once at time of booting

  if (indicator != nullptr)
    indicator->setup(true); // TODO: move it to caller

#ifdef USE_GSM
  clientSetup = myGsm.setup(config.gsm);
#else
  MyWiFiManager::OnAutoConnect onAutoConnect;
  {
    using Event = MyWiFiManager::AutoConnectEvent; 
    onAutoConnect = [&](Event e) {
      switch(e) {
        case Event::Entered:
          {
            auto indicator = getIndicator();
            if (indicator != nullptr)
              indicator->set(indicator->FAST);  // blink fast to indicate config
          }
          break;
        default:  // as per connect state
          indicateState();
          break;
      }
    };
  }
  myWiFiManager.setup(config.wiFi, 
      [&]() { platformConfigstorage.save(config.wiFi); },
      onAutoConnect);
  clientSetup = true;
#endif

  topicMgr.formTopics(false, defaultTxTopics); // load default TxTopics
  Topics rxTopics;
  topicMgr.formTopics(true, rxTopics);

  mqttManager.setup(/*config.mqtt*/);
  mqttManager.set(clientId, rxTopics, qos, callback);

  otaManager = factory().makeOTAManager(OTA_URL, systemInfo.filename);
  if (otaManager != nullptr) {
    otaManager->setup(); // Start FOTA Service
  }

  clock.setup();
  {
    auto time = clock.now(false);  // get saved clock;
    auto synchronized = time > 0;
    if (!synchronized) { // use systemInfo time to adjust to, in case clock fails
      DateTime adjustTime(systemInfo.date, systemInfo.time);
      time = adjustTime.unixtime();
    }
    CalendarUtils::setup(&clock, time, synchronized);
  }

  setOnStateChange(nullptr);  // set default actions on state change
}

void Platform::setOnStateChange(std::function<void (State st)> moreAction) {
  onStateChange = [=](State st) {
    indicateState(st);

    /* when newly connected to network, force Calendar if needed */
    if (st == NetOnly && !CalendarUtils::synchronized()) {
      CalendarUtils::synchronize(true);
      PRINTLN("Time:", CalendarUtils::toString(CalendarUtils::now()));
    }

    if (moreAction != nullptr)
      moreAction(st);
  };
}

void Platform::indicateState(State state) {
    int connectState = (int)state;
    static const char* patterns[] {
      "101010100000", // 4 blinks when setup error
      "101010000000", // 3 blinks when none connected
      "101000000000", // 2 blinks when only network connected
      "100000000000", // 1 blink when all connected
    };
    if (connectState < 0 || connectState >= sizeof(patterns)/sizeof(patterns[0])) {
      connectState = 0;
    }
    if (indicator != nullptr)
      indicator->set(indicator->FAST, patterns[connectState]);
}

String Platform::defaultTxTopic() {
  static String topic = "";
  if (topic != "")
    return topic;

  for (auto& rc: defaultTxTopics) {
    topic = rc;
    break;
  }
  return topic;
}

#include "task.hpp"
void Platform::sendMsg(String topic, String payld)
{
  if (topic == "")
    topic = defaultTxTopic();

  taskManager.enqueue([=] () {
    auto rc = mqttManager.sendMsg(topic, payld);
    if (!rc) {
      WARN("Send failure", topic, payld);
    }
    // return rc;  // if fail, then it will attempt to publish again
    return true;  // if fail, the message is ignored
  }, "MQTT::sendMsg");
}

void Platform::sendMsg(String payld) {
  for (auto& topic: defaultTxTopics) {
    sendMsg(topic, payld);
  }
}

extern String VERSION();

const char* Platform::connect() {
  stateChanged();

  if (disabled) {
    return "Disabled";
  }

  if (!clientSetup) {
    // TRACE("Network: not setup correctly");
    return "Not setup right";
  }

  if (myClient.connected()) {
    // TRACE("Network: already connected");
  } else {
    //TRACE("Network: connecting");
    bool rc = myClient.connect();
    if (!rc) {
      //WARN("Network: connection failed");
      return "Network connection failed";
    }
  }

  stateChanged();

  if (mqttManager.isConnected()) {
    // TRACE("MQTT: already connected")
  } else {
    //TRACE("MQTT: connecting");
    auto rc = mqttManager.connect();
    if (!rc) {
      //WARN("MQTT: connection failed");
      return "MQTT connection failed";
    }
  }

  stateChanged();

  return nullptr;
}

static const char sSetupError[] PROGMEM = "Broker/network setup issue";
static const char sNotConnected[] PROGMEM = "Network not connected";
static const char sNetOnly[] PROGMEM = "Network connected";
static const char sNetNServer[] PROGMEM = "Broker connected";
static const char* sStates[] PROGMEM = {
  sSetupError, sNotConnected, sNetOnly, sNetNServer,
};
const char* Platform::toText(State st) {
  if (st < SetupError)
    return nullptr;
  if (st > NetNServer)
    return nullptr;
  return sStates[st];
}

Platform::State Platform::getConnectState()
{
  if (!myClient.connected())
    return NotConnected;
  if (!mqttManager.isConnected())
    return NetOnly;
  return NetNServer;
}

void Platform::loop() {
  if (disabled)
    return;

  loopMqtt();

  loopOTA();
}

void Platform::loopMqtt() {
  if (!isConnected()) {
    yield();

    //TRACE("Fixing connections...");
    auto res = connect();
    if (res != nullptr) {
      //WARN("Connecting unsuccessful", res);
      return;
    }

    yield();
  }

  myClient.loop();
  yield();

  mqttManager.loop();
  yield();
}

void Platform::loopOTA() {
  if (otaManager != nullptr)
    otaManager->loop(); // Service FOTA using IDE Requests
}

void Platform::setDisabled(bool disabled) {
  this->disabled = disabled;
  TRACE("platform::disabled", disabled);

  if (disabled) {
    mqttManager.disconnect();
  }
}

#include "task.hpp"

void Platform::reboot(const char* cause, bool save) {
  TRACE("Platform::reboot", cause, onReboot != nullptr);

  if (onReboot != nullptr) {
    onReboot(save);
  }

  taskManager.enqueue([=] () {
      PRINTLN("******* Rebooting");
      delay(5000);
      ESP.restart();
      delay(5000);

      return true;
  }, "Platform::reboot");
}


void Platform::deepSleep(uint32_t secs, const char* cause, bool save) {
  TRACE("Platform::deepsleep", cause, onReboot != nullptr);

#ifdef MAX_DEEP_SLEEP
  if (secs > MAX_DEEP_SLEEP) {
    TRACE("deepSleep reduced from", secs, "to", MAX_DEEP_SLEEP);
    secs = MAX_DEEP_SLEEP;
  }
#endif

  if (onReboot != nullptr) {
    onReboot(save);
  }

  taskManager.enqueue([=] () {
      PRINTLN("******* Deepsleeping", secs);
      delay(5000);
      ESP.deepSleep((uint64_t)(secs*1e6));
      delay(5000);
      WARN("Deepsleep didnt work?", secs);

      return true;
  }, "Platform::deepsleep");
}

#include "payload.hpp"
void Platform::setSystemInfo(JsonDocument& doc) {
    doc["ip"] = MiscUtils::ipToString(_factory.getClient().localIP());
#ifdef USE_GSM
    {
      auto& myGsm = (MyGsm&)_factory.getClient();
      if (myGsm.checkSim()) {
        auto& modem = myGsm._modem();
        doc["ccid"] = modem.getSimCCID();
        doc["imei"] = modem.getIMEI();
        if (modem.isNetworkConnected()) {
          doc["csq"] = modem.getSignalQuality();
        }
      }
    }
#endif
    doc["rmac"] = MiscUtils::formMAC();
    doc["PCB"] = 
#ifdef ESP8266
      "ESP8266"
#elif ESP32
      "ESP32"
#endif
#ifdef USE_GSM
      "_SIM800"
#else
      "_WIFI"
#endif
      + String(clock.hasBegun()? "_RTC": "")
#ifdef PCB_TYPE
      + "_" + PCB_TYPE
#endif
      ;
    doc["File"] = MiscUtils::basename(systemInfo.filename);
    doc["Version"] = VERSION();
    doc["BuildTime"] = systemInfo.date + String(" - ") + systemInfo.time; 
}

void Platform::stateChanged() {
  if (onStateChange == nullptr)
    return;

  auto state = getConnectState();
  if (currentState == state)
    return;

  TRACE("Platform::state", state, currentState);
  if (state == NetNServer) // has become online
     TRACE("System Online.. Network and MQTT broker connected");
  else if (state < currentState) // regressed
     WARN("System offline: connection regressed from", currentState, "to", state);

  onStateChange(state);

  currentState = state;
}
