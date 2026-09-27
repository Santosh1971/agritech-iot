#pragma once

#include "MQTTManager.hpp"
#include "LEDController.hpp"

#include "platformConfig.hpp"
#include "EEPROMStorage.hpp"
extern EEPROMStorage<PlatformConfig> platformConfigstorage;

#ifndef PLATFORM_STORAGE_MARKER
  #define PLATFORM_STORAGE_MARKER 64
#endif

#include "topicMgr.hpp"

#include <ArduinoJson.h>
#include <RTClib.h>

#include <functional>

// temporary holder for all global variables till they are cleaned up
extern const int http_port;

#include "timeUtils.hpp"
#include "payload.hpp"

#include "myClient.hpp"
#include "MQTTManager.hpp"
#include "LEDController.hpp"

#include "RTCClock.hpp"
#include "OTAManager.hpp"

/**
 * Class providing top level services and interactions with platform
 * and internalizing GSM and WiFi variations.
 * It also sets up RTC and OTA services, and CalendarUtils.
 *
 * Variables:
 * Topics: Rx and Tx combinations
 * 
 * Functions:
 * * send as a sub-task; different variations
 * * send to default topic(s)
 * * onStatusChange(Disconnected, ToNeworkOnly, Connected)
 */
class Platform {
  public:
    using SystemInfo = struct { // for startup message
      const char* filename;
      const char* date;
      const char* time;
    };

  private:
    String clientId;
    SystemInfo systemInfo;

    MyClient& myClient;
    bool clientSetup;

    MQTTManager mqttManager;
    LEDController* indicator;

    RTCClock clock;
    OTAManager* otaManager;

  public:
    Platform(String clientId, SystemInfo systemInfo, LEDController* indicator=nullptr);

    /**
     * An abstract class providing key services which vary with GSM and WiFi
     * Internalizes switching between GSM and WiFi
     */
    class Factory {
    public:
      /**
       * Get client for connecting to MQTT
       */
      virtual MyClient& getClient()=0;

      /**
       * Get access to a clock for getting time from that connection
       */
      virtual MyClock& getClock()=0;

      /*
       * Make Manager for OTA from OTA URL for the file.
       * @return nullptr if OTA is not supported
       */
      virtual OTAManager* makeOTAManager(const char* otaUrl, const char* filename)=0;
    };

    /**
     * factory for key services
     */
    Factory& factory();

    enum State {
      NetNServer = 3,  // both Client and Net connected
      NetOnly = 2, // only Net connected
      NotConnected = 1, // none connected
      SetupError = 0, // Setup issue (No SIM, WiFi AP not set etc.)
    };
    static const char* toText(State st);  // a textual description

    State getConnectState();  // get connected state
    bool isConnected() { // true if Net and PubSubClient connected
      return getConnectState() == NetNServer;
    }
    const char* connect();
    /*
     * if @disabled, disconnect from MQTT/Network. and will not connect again
     * If false, flag is turned off. And, will connect as per due loop
     * Primarily for situations to save power
     */
    void setDisabled(bool disabled = true);
    bool isDisabled() {
      return disabled;
    }

    // what is to be done in addition to LED changes etc.
    using OnStateChange = std::function<void (State newState)>;
    OnStateChange onStateChange;
    void setOnStateChange(std::function<void (State st)>);

    using Callback = MQTTManager::Callback;
    using Topics = MQTTManager::Topics;

    template <typename _Processor, typename _Messenger>
    static Callback makeCallback(_Messenger& messenger, TopicMgr& mgr) {
      return [&](String topic, String payld) {
        DynamicPayload root(4096);
        if (!root.deserialize(payld)) {
          //ERROR("Malformed payload", payld);
          Console.println("Malformed payload");
          return;
        }

        _Processor processor(root, topic, mgr.txTopic(topic), messenger);
        if (!processor.process()) {
          //ERROR("Unrecognized payload", payld);
          Console.println("Error in processing payload");
          return;
        }
      };
    }

      // as recognized by platform
    String getClientId() {
      return clientId;
    }

    // the logical device id; normally defaulting to macid
    String getDeviceId();

    RTCClock& getClock() {
      return clock;
    }
    OTAManager* getOtaManager() {
      return otaManager;
    }
    LEDController* getIndicator() {
      return indicator;
    }

    void setup(TopicMgr& topicMgr, Callback callback, uint8_t qos=1);

    void loop();

    /**
     * Trigger to be called on reboot
     */
    using OnReboot = std::function<void (bool save)>;
    OnReboot onReboot = nullptr;

    /**
     * Reboot using ESP.restart
     * Call onReboot before if defined, with save
     * Actual reboot can happen "offline", to clear any pending tasks
     * @cause: for logging
     * @save: save state; passed to onReboot
     */
    void reboot(const char* cause=nullptr, bool save=false);

    /**
     * Sleep using ESP.deepSleep
     * Call onReboot before if defined, with save
     * Actual sleep can happen "offline", to clear any pending tasks
     * @secs: #seconds to sleep (this is converted to micro-secs internally)
     * @cause: for logging
     * @save: save state; passed to onReboot
     */
    void deepSleep(uint32_t secs, const char* cause=nullptr, bool save=false);

  // private:
    void indicateState(State st);
    void indicateState() {
      indicateState(getConnectState());
    }

    // will send as a subtask
    void sendMsg(String payload); // to default topic(s)
    void sendMsg(String topic, String payload); // to a specific topic

    // regarding systemInfo payload
    void setSystemInfo(JsonDocument& doc);

    /**
     * send to this in some cases
     * CHECKME: is this really needed?
     */
    String defaultTxTopic();

  private:
    void setupConfig();

    void loopMqtt();
    void loopOTA();

    void stateChanged();

    Topics defaultTxTopics;  // normally one; perhaps more
    State currentState;
    bool disabled; // true => network and MQTT connection are not attempted
};

extern Platform platform;
