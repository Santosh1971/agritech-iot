#include "baseMQTTMessenger.hpp"

#include "platform.hpp"

#define DEBUG
#include "debug.hpp"

#include "payload.hpp"

BaseMQTTMessenger::BaseMQTTMessenger(String defaultMacId):
 defaultMacId(defaultMacId) {
}


void BaseMQTTMessenger::sendSystemInfo(String token, String txTopic) {
  StaticPayload<600> doc("SystemInfo", getMacId());

  platform.setSystemInfo(doc);

  addProductInfo(doc);

  if (token != "")
    doc["Token"] = token;

  if (txTopic != "")
    sendPayload(txTopic, doc.serialize());
  else
    sendPayload(doc.serialize());
}


void BaseMQTTMessenger::sendAck(String Token, String topic, const char* detailMsg)
{
  StaticPayload<500> doc("AckCommand", getMacId());

  doc["Token"] = Token;
  if (detailMsg != nullptr) {
    doc["Detail"] = detailMsg;
  }
  
  sendPayload(topic, doc.serialize());
}

void BaseMQTTMessenger::sendError(String Token, String errMsg, String topic) {
  StaticPayload<500> doc("ErrCommand", getMacId());

  doc["Token"] = Token;
  doc["Err"] = errMsg;

  sendPayload(topic, doc.serialize());
}

void BaseMQTTMessenger::sendError(String Token, const char* errMsg, String topic) {
  String smsg = errMsg;
  sendError(Token, smsg, topic);
}

void BaseMQTTMessenger::sendError(String Error_string)
{
  StaticPayload<400> root("GHErr", getMacId());
  root["Err"] = Error_string;
  sendPayload(root.serialize());
}

void BaseMQTTMessenger::sendEvent(const char* description) {
  StaticPayload<500> doc("DeviceEvent", getMacId());
  doc["description"] =  description;
  sendPayload(doc.serialize());
}

void BaseMQTTMessenger::sendEvent(String description) {
  StaticPayload<500> doc("DeviceEvent", getMacId());
  doc["description"] =  description;
  sendPayload(doc.serialize());
}

void BaseMQTTMessenger::sendPayload(String payload) {
  platform.sendMsg(payload);
}

void BaseMQTTMessenger::sendPayload(String topic, String payload) {
  platform.sendMsg(topic, payload);
}

bool BaseMQTTMessenger::isConnected() {
  return platform.isConnected();
}

void BaseMQTTMessenger::addProductInfo(JsonDocument& doc) {
  // nothing as a default
}

String BaseMQTTMessenger::getMacId() {
  if (defaultMacId != "")
    return defaultMacId;
  return platform.getDeviceId();
}
