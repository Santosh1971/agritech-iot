#pragma once

#include <Arduino.h>

#include "payload.hpp"
#include "baseMQTTMessenger.hpp"

class BaseMQTTProcessor {
protected:
  JsonDocument& doc;
  String payloadId;
  String topic;
  String txTopic;

  BaseMQTTMessenger& mqttMessenger;

  // a few  common fields
  String mac_nodeid;

public:
  BaseMQTTProcessor(JsonDocument& doc, String topic, String txTopic, BaseMQTTMessenger& messenger);

  virtual bool process();

protected:
  void processSystemInfo();
  void upload();
  void setDateTime();
  void processSystemReset();
  void processDeviceCommand();

  // acknowledgements
  void sendAck(const char* detailMsg=nullptr);
  void sendAck(String detailMsg);

  // negative acknowledgements
  void sendError(const char* err); // send an error message
  void sendError(String err); // send an error message

  void sendNotImplemented();  // send stock "TBD" message

  /**
   * A couple of convenient methods to extract fields from the payload
   */
  template <typename T>
  static T extractField(JsonObject& o, const char* field, T defolt) {
    JsonVariant val = o[field];
    return val.is<T>()? val.as<T>(): defolt;
  }
  template <typename T>
  static T extractField(JsonDocument& doc, const char* field, T defolt) {
    return extractField<T>(doc.as<JsonObject>(), field, defolt);
  }

private:
  void preprocess();  // parse input for common fields etc.
};
