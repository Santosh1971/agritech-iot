#pragma once
#include <Arduino.h>
#include <RTClib.h>
#include <time.h>

class RTCManager {
public:
    bool begin();
    DateTime now();
    void syncFromUnix(uint32_t unixTime);
    void syncFromTm(struct tm& timeinfo);   // write IST fields directly
    bool isTimeSet();
    String getTimeString();
    String getDateString();
    uint32_t getUnixTime();
private:
    RTC_DS3231 _rtc;
    bool _initialized = false;
};
