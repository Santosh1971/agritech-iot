#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>

#include <PubSubClient.h>         //MQTT server library  

#include "myClient.hpp"

class MQTTManager {
  MyClient& myClient;  // either WiFi or GSM
  String clientId;

  const char* broker;
  int port;
  bool pwEnabled;
  const char* userName;
  const char* password;

  PubSubClient client;

  MQTTManager(MyClient& myClient, const char* broker, int port, bool pwEnabled, const char* userName, const char* password);

  public:
    using Callback = std::function<void (String topic, String payload)>;
    using Topics = std::vector<String>;

    MQTTManager(MyClient& myClient, const char* broker, int port);

    MQTTManager(MyClient& myClient, const char* broker, int port, const char* userName, const char* password);

    void set(String clientId, Topics& rxTopics, uint8_t qos, Callback callback);
 
    void setup();

    void loop(void);

    //String clientId();

    /**
     * connect to server
     * @return nullptr on success; or error message on failure
     */
    bool connect();

    /**
     * disconnect from server
     * normally called only for testing purposes
     */
    void disconnect();

    bool isConnected();

    bool sendMsg(String topic, String payld) ; // on a specific topic

  private:
    void processReceived(char* topic, byte* payload, unsigned int length);

    bool connectToServer();
    void subscribeToTopics();

  Topics rxTopics;
  uint8_t qos;
  Callback callback;
};
