#ifndef _PAYLOAD_HPP
#define _PAYLOAD_HPP

#include <ArduinoJson.h> // CAUTION: this has no guard against repeated includes

#include "miscUtils.hpp"

#include "debug.hpp"

/*
 * @Note: using const char* as template parameter should be such that
 * it is done at compile time.
 * Ref: https://stackoverflow.com/questions/28809728/some-const-char-are-unavailable-at-compile-time/28810320#28810320
 * Ref: c++11 standard 14.3.2.1
 */
template <size_t CAPACITY, const char* _PID_KEY = nullptr>
class StaticPayload : public StaticJsonDocument<CAPACITY> {
private:
  String mac;   // TODO: standardize use of mac in messages

public:
  /**
   * Constructor used primarily for creation before serialization
   */
  StaticPayload(String payloadId, String mac): mac(mac) {
    (*this)[_PID_KEY == nullptr? "payloadId": _PID_KEY] = payloadId;
  }

  /**
   * Constructor used primarily in deserialization
   */
  StaticPayload() {
  }

  String serialize(bool tsNeeded=true) {
    if (tsNeeded) {
      addTimestamp();
    }

    // (*this)["mac"] = mqtt_client_id;   //"5ccf7f3c70ae";
    if (mac.length() > 0) {
      (*this)["mac"] = mac.c_str();
    }

    char bodyString[CAPACITY];
    serializeJson(*this, bodyString);

    TRACE(F("Payload to send:"), bodyString);
    return bodyString;  // TODO: inefficient
  }

  /**
   * Return true on success; false on failure
   */
  bool deserialize(const String& jsonString) {
    DeserializationError error = deserializeJson(*this, jsonString);
    if (error) {
      ERROR(F("deserializeJson() failed:"), error.c_str(), _NL, jsonString);
      return false;
    }

    TRACE(F("Payload received and validated:"), jsonString);
    return true;
  }

private:
  void addTimestamp() {
    (*this)["Date"] = MiscUtils::timeToString();
  }
};

class DynamicPayload : public DynamicJsonDocument {
private:
  String mac;   // TODO: standardize use of mac in messages

public:
  /**
   * Constructor used primarily for creation before serialization
   */
  DynamicPayload(size_t capa, String payloadId, String mac): DynamicJsonDocument(capa), mac(mac) {
    (*this)["payloadId"] = payloadId;
  }

  /**
   * Constructor used primarily in deserialization
   */
  DynamicPayload(size_t capa): DynamicJsonDocument(capa) {
  }

  String serialize(bool tsNeeded=true) {
    if (tsNeeded) {
      addTimestamp();
    }

    // (*this)["mac"] = mqtt_client_id;   //"5ccf7f3c70ae";
    if (mac.length() > 0) {
      (*this)["mac"] = mac.c_str();
    }

    String bodyString;
    serializeJson(*this, bodyString);

    TRACE(F("Payload to send:"), bodyString);
    return bodyString;  // TODO: inefficient
  }

  /**
   * Return true on success; false on failure
   */
  bool deserialize(String& jsonString) {
    DeserializationError error = deserializeJson(*this, jsonString);
    if (error) {
      ERROR(F("deserializeJson() failed: "), error.c_str(), _NL, jsonString);
      return false;
    }

    TRACE(F("Payload received and validated:"), jsonString);
    return true;
  }

private:
  void addTimestamp() {
    (*this)["Date"] = MiscUtils::timeToString();
  }
};

namespace Payload {
/*
 * round @f to #@decimalPlaces before serialization
 * @return @f after rounding
 * see https://arduinojson.org/v6/how-to/configure-the-serialization-of-floats/
 */
  extern double round(double f, uint8_t decimalplaces);
};

#endif
