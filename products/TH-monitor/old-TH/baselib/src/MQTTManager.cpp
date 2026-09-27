#include "MQTTManager.hpp"

#define DEBUG
#include "debug.hpp"

MQTTManager::MQTTManager(MyClient& myClient, const char* broker, int port, bool pwEnabled, const char* userName, const char* password):
  myClient(myClient), clientId("mqtt_default_client"),
  broker(broker), port(port), pwEnabled(pwEnabled), userName(userName), password(password),
  client(myClient.client()) {
}

MQTTManager::MQTTManager(MyClient& myClient, const char* broker, int port, const char* userName, const char* password):
  MQTTManager(myClient, broker, port, true, userName, password) {
}

MQTTManager::MQTTManager(MyClient& myClient, const char* broker, int port):
  MQTTManager(myClient, broker, port, false, "", "") {
}

void MQTTManager::set(String clientId, Topics& rxTopics, uint8_t qos, Callback callback) {
  this->clientId = clientId;
  this->rxTopics = rxTopics;
  this->qos = qos;
  this->callback  = callback;
}

void MQTTManager::setup(void) 
{
}

bool MQTTManager::connect(void) {
  if (!connectToServer())
    return false;

  subscribeToTopics();
  return true;
}

void MQTTManager::disconnect(void) {
  client.disconnect();
}

bool MQTTManager::isConnected(void) {
  return client.connected();
}


bool MQTTManager::connectToServer() {
  // Start Mqtt server & set callback
  client.setServer(broker, port);
  PRINTLN("Connecting to MQTT Server", broker, "port", port,
          pwEnabled? "with password": "without password");
  client.setCallback([this](char* topic, byte* payload, unsigned int length) {
      processReceived(topic, payload, length);
    });

  // Connect to MQTT Server
  bool connected = false;
  if (pwEnabled) {
    connected = client.connect(clientId.c_str(), userName, password);
  } else {
    connected = client.connect(clientId.c_str());
  }
  PRINTLN("MQTT server connect", connected? "succeeded": "failed");
  return connected;
}

void MQTTManager::subscribeToTopics() {
    PRINT("Subscribing:", "QOS", qos);
    for (auto rx: rxTopics) {
      PRINT(" ", rx);
      client.subscribe((char *)rx.c_str(), qos);
    }
    PRINTLN();
}

/**
 * MQTT loop() service
 */
void MQTTManager::loop(void) 
{
  client.loop();
}

#ifndef MQTT_MSG_RETAIN
#define MQTT_MSG_RETAIN false
#endif

bool MQTTManager::sendMsg(String topic, String payld)
{
    if (!isConnected()) {
      WARN(F("Not connected to broker; cannot send to platform"), payld, topic);
      return false;
    }

      bool res = client.publish((char *)topic.c_str(), (char *) payld.c_str(), MQTT_MSG_RETAIN);

        PRINTLN(F("-------------------------------------"));
        PRINTLN(F("MQTT Tx:  TOPIC ["), topic, "]");
        PRINTLN("Send Result", res);
        if (!res) {
          PRINTLN("Connected:", client.connected(),
                "Lengths: Payload:", payld.length(),
                "Topic:", topic.length());
        }
        PRINTLN(F("        PAYLOAD ["), payld, "]");
        PRINTLN(F("-------------------------------------"));

    return res;  // if fail, then it will attempt to publish again
}

#include "task.hpp"

/**
 * @topic        topic string
 * @payload      value received
 * @length       value length (bytes)
 */
void MQTTManager::processReceived(char* topic, byte* payload, unsigned int length)
{
  TRACE("MQTTManager::Received", topic, length);

// copy to avoid potential corruption, if payload/topic is recycled early
  String stopic(topic);
  String spayload = ""; // Extract payload
  for (unsigned int i = 0; i < length; i++) {
    spayload = spayload + String((char)payload[i]);
  }

  TRACE("\n", F("-------------------------------------\n"),
        F("MQTT Rx:  TOPIC ["), stopic, F("] \r\nPayload:"),
        F("]    PAYLOAD ["), spayload, F("]"),
        F("-------------------------------------"));

  if (callback != nullptr) {
    taskManager.enqueue([=]() {
      callback(stopic, spayload);
      return true;
    }, "Rx");
  }
}
