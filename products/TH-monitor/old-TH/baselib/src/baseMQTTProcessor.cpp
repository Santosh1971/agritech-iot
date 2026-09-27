#include "baseMQTTProcessor.hpp"

#include "baseMQTTMessenger.hpp"

#include "platform.hpp"

#include "task.hpp"

#define DEBUG
#include "debug.hpp"

BaseMQTTProcessor::BaseMQTTProcessor(JsonDocument& doc, String topic, String txTopic, BaseMQTTMessenger& messenger):
  doc(doc), topic(topic), txTopic(txTopic)
  , mqttMessenger(messenger)
{
  preprocess();

  TRACE(F("MQTT"),
      F("; payloadId:"), payloadId,
      F("; Topic:"), topic,
      F("; TxTopic:"), txTopic,
      F("; mac_nodeid:"), mac_nodeid);
}

// new logic
void BaseMQTTProcessor::preprocess() {
  const char* tmp = doc["payloadId"];
  payloadId = tmp;    //payloadId = doc["payloadId"];

  mac_nodeid = MiscUtils::basename(topic, '/');
}

bool BaseMQTTProcessor::process() {
  if(false) {
  } else if(payloadId == "SystemInfo") {
    processSystemInfo();
  } else if(payloadId == "SystemUpgrade") {
    upload();
  } else if(payloadId == "SetTime") {
    setDateTime();
  } else if(payloadId == "SystemReset") {
    processSystemReset();
  } else if(payloadId == "GWCMD") {
    processDeviceCommand();
  } else {
    return false;
  }
  return true;
}

void BaseMQTTProcessor::processSystemInfo() {
  String token = doc["Token"];
  mqttMessenger.sendSystemInfo(token, txTopic);
}

#include "platform.hpp"
/**
{"payloadId"  : "SystemUpgrade", 
 "filename": "OtherThanTheDefaultFile.ino",  // optional; if other than default
"Token" : "4d350975-4d01-4f8d-82be-14a84edd952c",
"Date" : "7/1/2019 16:20:51",
"mac" : "5ccf7f3d79d7_110"}
 */
void BaseMQTTProcessor::upload() {
  const char* filename = doc["filename"];
  TRACE("SystemUpgrade", filename);

  OTAManager* otaManager = platform.getOtaManager();
  if (otaManager == nullptr) {
    sendError("SystemUpgrade not available");
    return;
  }

  sendAck("Starting SystemUpgrade");
  String token = doc["token"];
  taskManager.enqueue([=] () {
    otaManager->start(filename 
#ifdef FIXME
// below might crash due to large# String copy; disabled pending refinement
, [=](String key, String val) {
        // mqttMessenger.sendAck(token, txTopic, nullptr, key + ":" + val);
        mqttMessenger.sendEvent(key + ":" + val);
      }
#endif
        ); // consider parallelization
    return true;
  }, "SystemUpgrade");
}

void BaseMQTTProcessor::setDateTime() {
  MiscUtils::setCurrentTime(
          doc["YYYY"], doc["MM"], doc["DD"],
          doc["hh"], doc["mm"], doc["ss"]);
  TRACE("SetTime:");
  sendAck("Current time set");
}

#include "platform.hpp"
extern Platform platform;

/**
{"payloadId":"SystemReset"}
*/
void BaseMQTTProcessor::processSystemReset() {
  sendAck("Resetting system");

  platform.reboot("SystemReset");
}

void BaseMQTTProcessor::processDeviceCommand() {
  String nid = doc["nid"];
  if (nid.length() <= 0) {
    sendError("nid is not provided");
    return;
  }
  auto& values = platformConfigstorage.values();
  auto& deviceId = values.deviceId;
  const auto max = sizeof(platformConfigstorage.values().deviceId);
  if (nid.length() >= max) {
    sendError(String("nid is too long") + nid.length() + String(" must be < ") + max);
    return;
  }
  ::strcpy(deviceId, nid.c_str());
  TRACE("nid changed to", deviceId);
  platformConfigstorage.save(deviceId);

  sendAck("Changed device id to " + nid + " and rebooting");

  platform.reboot("SystemReset");
}

void BaseMQTTProcessor::sendAck(const char* detailMsg) {
  mqttMessenger.sendAck(doc["Token"], txTopic, detailMsg);
}

void BaseMQTTProcessor::sendAck(String detailMsg) {
  sendAck(detailMsg.c_str());
}

void BaseMQTTProcessor::sendError(String err) {
  mqttMessenger.sendError(doc["Token"], err, txTopic);
}

void BaseMQTTProcessor::sendError(const char* err) {
  sendError(String(err));
}

void BaseMQTTProcessor::sendNotImplemented() {
  mqttMessenger.sendError("Sorry! this message is not yet supported:" + payloadId);
}
