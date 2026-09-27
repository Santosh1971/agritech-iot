#pragma once

#include <Arduino.h>
#include <vector>

class TopicMgr {
  public:
    /**
     * form topics for use
     * @isRx: if true, return the topics to be subscribed;
     *        if false, return the default topics to publish messages
     * @topics: the topics will be appended to this list
     * @return true; false in case of any failure in forming topics
     */
    virtual bool formTopics(bool isRx, std::vector<String>& topics) = 0;

    /**
     * Implements a common pattern of topics; for use in subclasses
     * @isRx: as in earlier formTopics
     * @topics: as in earlier formTopics method
     * @appSuffix: application specific string added to topic; e.g. SM01, TH01 etc.
     * @deviceId: a logical device id if used as alternate to mac
     * @topicStage: 0/1/2 for dev/test/prod; -1 if you dont want it to be used
     */

    // older pattern
    bool formTopics(bool isRx, std::vector<String>& topics,
      int topicStage, const char* appSuffix);

    // newer more generic pattern
    bool formTopics(bool isRx, std::vector<String>& topics,
      const char* appSuffix, const char* deviceId=nullptr, int topicStage=-1);

    /**
     * determine txTopic for a rxTopic
     * * rxTopic: the rxtopic
     * @return txTopic
     */
    String txTopic(String rxTopic);
};
