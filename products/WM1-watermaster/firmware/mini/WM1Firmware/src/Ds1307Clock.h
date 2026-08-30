#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <time.h>

// Same manual-I2C DS1307 access the bench sketches already proved out,
// wrapped as a class so main.cpp and CommandHandler can share one
// instance instead of duplicating free functions.
class Ds1307Clock {
public:
  static constexpr uint8_t ADDR = 0x68;

  bool begin() { return _resync(); }

  // millis()-derived, re-based from the chip once at begin()/every
  // syncFromUnix() — matches the bench sketches' approach so behavior
  // (including the clock-jump guard in Scheduler) doesn't change.
  time_t now() { return _baseEpoch + (millis() - _baseMillis) / 1000; }

  // CH (Clock Halt) bit, register 0 bit 7 — 0 means the oscillator is
  // running. A DS1307 that has never been set (or lost its backup
  // battery) reads this as halted, which is the "RTC not set, schedules
  // won't trigger" signal shown on the Dashboard.
  bool isRunning() {
    uint8_t raw;
    if (!_readReg(0, raw)) return false;
    return (raw & 0x80) == 0;
  }

  String dateString() {
    struct tm t;
    if (!_readFull(t)) return "--/--/----";
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    return String(buf);
  }

  String timeString() {
    struct tm t;
    if (!_readFull(t)) return "--:--";
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
  }

  // unixTime follows FG1's established convention (RTCManager::syncFromUnix
  // paired with the app's rtc_sync call): the app reinterprets the
  // phone's LOCAL wall-clock fields as if they were UTC before sending,
  // because this RTC stores raw wall-clock, not a true UTC epoch.
  // gmtime() here undoes that same reinterpretation, so the two sides
  // cancel out. Do not "fix" this to a real timezone conversion on only
  // one side — that would shift every scheduled time by the local
  // UTC offset.
  void syncFromUnix(uint32_t unixTime) {
    time_t t = (time_t)unixTime;
    struct tm utc;
    gmtime_r(&t, &utc);
    _writeFull(utc);
    _resync();
    Serial.printf("[RTC] Synced from phone — %s %s\n", dateString().c_str(), timeString().c_str());
  }

private:
  static uint8_t _bcd2dec(uint8_t v) { return ((v / 16) * 10) + (v % 16); }
  static uint8_t _dec2bcd(uint8_t v) { return ((v / 10) * 16) + (v % 10); }

  bool _readReg(uint8_t reg, uint8_t& out) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom(ADDR, 1);
    if (Wire.available() < 1) return false;
    out = Wire.read();
    return true;
  }

  bool _readFull(struct tm& out) {
    Wire.beginTransmission(ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom(ADDR, 7);
    if (Wire.available() < 7) return false;
    out.tm_sec  = _bcd2dec(Wire.read() & 0x7F);
    out.tm_min  = _bcd2dec(Wire.read());
    out.tm_hour = _bcd2dec(Wire.read() & 0x3F);
    Wire.read();  // day-of-week, unused — we compute our own
    out.tm_mday = _bcd2dec(Wire.read());
    out.tm_mon  = _bcd2dec(Wire.read()) - 1;
    out.tm_year = _bcd2dec(Wire.read()) + 100;
    out.tm_isdst = 0;
    return true;
  }

  void _writeFull(struct tm& t) {
    Wire.beginTransmission(ADDR);
    Wire.write(0x00);
    Wire.write(_dec2bcd(t.tm_sec));
    Wire.write(_dec2bcd(t.tm_min));
    Wire.write(_dec2bcd(t.tm_hour));
    Wire.write(1);  // day-of-week, unused by our code
    Wire.write(_dec2bcd(t.tm_mday));
    Wire.write(_dec2bcd(t.tm_mon + 1));
    Wire.write(_dec2bcd(t.tm_year - 100));
    Wire.endTransmission();
  }

  bool _resync() {
    struct tm t;
    if (!_readFull(t)) return false;
    _baseEpoch = mktime(&t);
    _baseMillis = millis();
    return true;
  }

  time_t _baseEpoch = 0;
  uint32_t _baseMillis = 0;
};
