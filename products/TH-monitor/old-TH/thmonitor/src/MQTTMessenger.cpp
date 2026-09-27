#include "MQTTMessenger.hpp"

#include "miscUtils.hpp"
#include "payload.hpp"
#include "platform.hpp"

#define DEBUG
#include "debug.hpp"

#include "payload.hpp"

#include "globals.h"

#include "miscUtils.hpp"
#include <stdio.h>
static String formatTs(Time ts) {
#if 0
  DateTime tm(ts);
  char s[30];
  int rc = sprintf(s, "%02u:%02u:%02u:%02u:%02u:%02u",
      tm.year()-2000, tm.month(), tm.day(), tm.hour(), tm.minute(), tm.second());
  if (rc != 17)
    return CalendarUtils::toString(ts);
  return s;
#else
  return MiscUtils::timeToString(ts);
#endif
}


void MQTTMessenger::sendTH(float temp, float humi, Time time, unsigned int seqNum, bool live, String token, String txTopic) {
  StaticPayload<500> doc("TH", txMac());
  doc["Temp"] = Payload::round(temp, 1);
  doc["Humi"] = round(humi);  // expects an integer humidity
  if (time > 0)
    doc["ts"] = formatTs(time);
  if (seqNum > 0)
    doc["seqNum"] = seqNum;
  doc["live"] = live? 1: 0;
  doc["rmac"] = MiscUtils::formMAC();

  if (token != "")
    doc["Token"] = token;

  sendPayload(txTopic, doc.serialize());
}

void MQTTMessenger::sendParameter(String token, String txTopic)
{
  StaticPayload<300> doc("Parameter", txMac());

  if (token != "")
    doc["Token"] = token;

  auto& values = EEPROMstorage.values();

  doc["StoreLimit"] = values.THstore.limit;
  doc["TempInterval"] = values.TempInterval;

  platform.sendMsg(txTopic, doc.serialize());
}


void MQTTMessenger::sendAck(String Token, String topic, const char* detailMsg)
{
  StaticPayload<500> doc("AckCommand", txMac());

  doc["Token"] = Token;
  if (detailMsg != nullptr) {
    doc["Detail"] = detailMsg;
  }

  sendPayload(topic, doc.serialize());
}

void MQTTMessenger::sendError(String Token, const char* errMsg, String topic) {
  StaticPayload<500> doc("ErrCommand", txMac());

  doc["Token"] = Token;
  doc["Err"] = errMsg;

}

void MQTTMessenger::sendError(String Token, String errMsg, String topic) {
  sendError(Token, errMsg.c_str(), topic);
}

void MQTTMessenger::sendError(String Error_string)
{
  StaticPayload<400> root("GHErr", txMac());
  root["Err"] = Error_string;
  platform.sendMsg(root.serialize());
}

void MQTTMessenger::sendEvent(const char* description) {
  StaticPayload<500> doc("DeviceEvent", txMac());
  doc["description"] =  description;
  platform.sendMsg(doc.serialize());
}

void MQTTMessenger::sendEvent(String description) {
  StaticPayload<500> doc("DeviceEvent", txMac());
  doc["description"] =  description;
  platform.sendMsg(doc.serialize());
}

void MQTTMessenger::sendPayload(String payload) {
  platform.sendMsg(payload);
}

void MQTTMessenger::sendPayload(String topic, String payload) {
  if (topic == "")
    platform.sendMsg(payload);
  else
    platform.sendMsg(topic, payload);
}

String MQTTMessenger::txMac() {
  // Always send on same mac for now
  return getMacId();
}
