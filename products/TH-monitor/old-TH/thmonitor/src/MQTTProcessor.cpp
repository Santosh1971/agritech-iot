#include "MQTTProcessor.hpp"

#include "MQTTMessenger.hpp"

#include "globals.h"

#include "task.hpp"

#include "calendarUtils.hpp"

#define DEBUG
#include "debug.hpp"

MQTTProcessor::MQTTProcessor(JsonDocument& doc, String topic, String txTopic,
                             MQTTMessenger& messenger)
    : BaseMQTTProcessor(doc, topic, txTopic, messenger) {}

#if 0
// new logic
void MQTTProcessor::preprocess() {
  mac_nodeid = MiscUtils::basename(topic, '/');

#if 0
  int underscorePosition = mac_nodeid.indexOf('_');
  String nid_string = mac_nodeid.substring(underscorePosition+1);
 // nid = (mac_nodeid.substring(underscorePosition)).toInt();    // substring(index) looks for the substring from the index position to the end:
  nid = nid_string.toInt();
#else
  nid = 110;  // TODO: remove hard wiring
#endif
}
#endif

bool MQTTProcessor::process() {
  if (BaseMQTTProcessor::process()) {
    return true;
  } else if(payloadId == "SetParameter") { //  {"payloadId":"SetParameter"}
    setParameter();
  } else if(payloadId == "GetParameter") {
    getParameter();
  } else if(payloadId == "GetTH") {
    getTH();
  } else {
    WARN(F("Invalid MQTT Payloadid:"), payloadId);
    return false;
  }
  return true;
}

#include "myBitset.hpp"

static int extractField(JsonObject o, const char* field, int defoult) {
  JsonVariant val = o[field];
  return val.is<int>()? val.as<int>(): defoult;
}

#include "THSender.hpp"
#include "THStore.hpp"

/**
{"payloadId"  : "SetParameter", 
"Token" : "4d350975-4d01-4f8d-82be-14a84edd952c",
"TempInterval" : 60,  // seconds
"StoreLimit" : 100,  // rows
"Date" : "7/1/2019 16:20:51",
"mac" : "5ccf7f3d79d7_110"}
 */
void MQTTProcessor::setParameter() {
  String resp = "Set parameters: ", delim="";
  auto& values = EEPROMstorage.values();

  auto o = doc.as<JsonObject>();
  int interval = extractField(o, "TempInterval", -1);
  if (interval >= 0) {
    values.TempInterval = interval;
    EEPROMstorage.save(values.TempInterval);
    resp = resp + delim + "TempInterval=" + values.TempInterval;
    delim = ",";

    extern THSender thSender;
    thSender.setReadCycle(values.TempInterval);
  }

  int limit = extractField(o, "StoreLimit", -1);
  if (limit > 0) {
    values.THstore.limit = limit;
    EEPROMstorage.save(values.THstore.limit);
    resp = resp + delim + "StoreLimit=" + values.THstore.limit;
    delim = ",";

    extern THStore thStore;
    thStore.setLimit(limit);
  }

#ifdef DEBUG
  Serial.print("EEPROM Parameters:");
  EEPROMstorage.values().dump();
  Serial.println();
#endif

  sendAck(resp);
}

/**
{"payloadId"  : "GetParameter", 
"Token" : "4d350975-4d01-4f8d-82be-14a84edd952c",
"Date" : "7/1/2019 16:20:51",
"mac" : "5ccf7f3d79d7_110"}
 */
void MQTTProcessor::getParameter() {
  String token = doc["Token"];
  ((MQTTMessenger&)mqttMessenger).sendParameter(token, txTopic);
}

#include "myDHT.hpp"

/**
 * {"payloadId"  : "GetTH", 
"Token" : "4d350975-4d01-4f8d-82be-14a84edd952c",
"Date" : "7/1/2019 16:20:51",
"mac" : "5ccf7f3d79d7_110"}
 */
void MQTTProcessor::getTH() {
  String token = doc["Token"];

  float temp = -1, humi = -1;
  if (!myDHT.read(temp, humi)) {
    sendError("Read TH Error");
    return;
  }

  ((MQTTMessenger&)mqttMessenger).sendTH(temp, humi, CalendarUtils::now(), 0, true, token, txTopic);
}
