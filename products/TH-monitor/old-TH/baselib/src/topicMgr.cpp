#include "topicMgr.hpp"

#include "miscUtils.hpp"

#define DEBUG
#include "debug.hpp"

String TopicMgr::txTopic(String rxTopic) {
  int ix = rxTopic.indexOf("rx");
  if (ix < 0) {
    WARN(rxTopic, "is not an rxTopic");
    return rxTopic;
  }
  String out = rxTopic.substring(0, ix) + "tx" + rxTopic.substring(ix+2);
  return out;
}

bool TopicMgr::formTopics(bool isRx, std::vector<String>& topics,
    int topicStage, const char* appSuffix) {
  return formTopics(isRx, topics, appSuffix, nullptr, topicStage);
}

bool TopicMgr::formTopics(bool isRx, std::vector<String>& topics,
    const char* appSuffix, const char* deviceId, int topicStage) {
  static auto topicStageNames = { "dev", "test", "prod" };
  auto topicName = (topicStage >= 0 || topicStage < topicStageNames.size())?
        (*(topicStageNames.begin() + topicStage)): nullptr;

  String devId = deviceId? deviceId: MiscUtils::formMAC();

  const char* rx = isRx? "rx": "tx";

  String topic = String("mqtt_") + rx +
      ((topicName==nullptr)? "": (String("/") + topicName))
      + "/" + appSuffix + "/" + devId;

  TRACE(F("addTopic"), rx, topic);

  topics.push_back(topic);

  return true;
}
