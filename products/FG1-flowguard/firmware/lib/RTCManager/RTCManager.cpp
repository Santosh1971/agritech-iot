#include "RTCManager.h"
#include "Config.h"

bool RTCManager::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
#if defined(RTC_CHIP_DS1307)
    if (!_rtc.begin()) {
        Serial.println("[RTC] DS1307 not found!");
        return false;
    }
    // DS1307 has no OSF/power-loss flag like DS3231's lostPower() — the
    // clock-halt bit (isrunning()) only reflects whether the oscillator
    // was ever started (e.g. brand-new chip, no coin cell fitted), not
    // whether main power was lost while a good backup battery kept it
    // running. So this is a weaker signal than DS3231 gave us — a dead/
    // absent coin cell can silently produce a stale-but-"running" clock.
    if (!_rtc.isrunning()) {
        Serial.println("[RTC] DS1307 oscillator not running — time not set");
        _initialized = false;
    } else {
        _initialized = true;
        Serial.printf("[RTC] Time: %s %s\n",
            getDateString().c_str(), getTimeString().c_str());
    }
#else
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
#endif
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
    // Write IST time fields directly to the RTC (DS3231 or DS1307) — no unix conversion
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
#if defined(RTC_CHIP_DS1307)
    return _initialized && _rtc.isrunning();
#else
    return _initialized && !_rtc.lostPower();
#endif
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
