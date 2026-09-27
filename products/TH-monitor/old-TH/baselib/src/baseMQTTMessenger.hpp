#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

class BaseMQTTMessenger {
  public:
    BaseMQTTMessenger(String defaultMacId="");

    void sendSystemInfo(String token="", String txTopic="");

    /**
     * Send acknowledgement
     * * detailMsg: additional details
     */
    void sendAck(String token, String txTopic, const char* detailMsg=nullptr);

    /**
     * Send negative response for a command
     * * errMsg: mandatory error msg
     */
    void sendError(String token, const char* errMsg, String txTopic);
    void sendError(String token, String errMsg, String txTopic);

    /**
     * Send error
     */
    void sendError(String Error_string);

    /**
     * send a textual event to platform, initiated by the platform
     */
    void sendEvent(const char* description);
    void sendEvent(String description);

    /**
     * determine if connected for sending purposes
     * short cut to the corresponding platform method
     */
    bool isConnected();

  protected:
    /**
     * Add product specific information to systemInfo
     */
    virtual void addProductInfo(JsonDocument& doc);

    /* send Payload */
    void sendPayload(String payload); // TODO: String ref
    void sendPayload(String topic, String payload); // TODO: String ref

    /* build the string to send as the device's macId */
    virtual String getMacId();

  private:
    String defaultMacId;
};
