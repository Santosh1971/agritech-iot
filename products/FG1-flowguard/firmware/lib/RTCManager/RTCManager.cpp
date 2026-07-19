#include "RTCManager.h"
#include "Config.h"

bool RTCManager::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!_rtc.begin()) {
        Serial.println("[RTC] DS3231 not found!");
        return false;
    }
    if (_rtc.lostPower()) {
        Serial.println("[RTC] Power lost — time not set");
        _initialized = false;
    } else {
        _initialized = true;
        Serial.printf("[RTC] Time: %s %s\n",
            getDateString().c_str(), getTimeString().c_str());
    }
    return true;
}

DateTime RTCManager::now() {
    return _rtc.now();
}

void RTCManager::syncFromUnix(uint32_t unixTime) {
    _rtc.adjust(DateTime(unixTime));
    _initialized = true;
    Serial.printf("[RTC] Synced — %s %s\n",
        getDateString().c_str(), getTimeString().c_str());
}

void RTCManager::syncFromTm(struct tm& t) {
    // Write IST time fields directly to DS3231 — no unix conversion
    _rtc.adjust(DateTime(
        t.tm_year + 1900,
        t.tm_mon  + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec
    ));
    _initialized = true;
    Serial.printf("[RTC] Synced from tm — %s %s\n",
        getDateString().c_str(), getTimeString().c_str());
}

bool RTCManager::isTimeSet() {
    return _initialized && !_rtc.lostPower();
}

String RTCManager::getTimeString() {
    DateTime now = _rtc.now();
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", now.hour(), now.minute());
    return String(buf);
}

String RTCManager::getDateString() {
    DateTime now = _rtc.now();
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d",
        now.day(), now.month(), now.year());
    return String(buf);
}

uint32_t RTCManager::getUnixTime() {
    return _rtc.now().unixtime();
}
