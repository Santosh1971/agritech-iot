#pragma once

#include <Arduino.h>

#include "payload.hpp"

#include "baseMQTTProcessor.hpp"
class MQTTMessenger;

class MQTTProcessor: public BaseMQTTProcessor {
private:

  // a few  common fields
  int nid;

public:
  MQTTProcessor(JsonDocument& doc, String topic, String txTopic, MQTTMessenger& messenger);

  // void preprocess();  // parse input for common fields etc.
  bool process();

private:
  void setParameter();
  void getParameter();
  void getTH();
};
