#pragma once
#include <Arduino.h>
#include <time.h>
#include "Ds1307Clock.h"

// Sets the DS1307 from internet time whenever the board is on WiFi (0.4.10,
// tester: "on WiFi the app says RTC not set"). Same servers and IST offset
// as FG1, non-blocking (polled from loop()).
//
// Important: this firmware keeps WALL-CLOCK time in the RTC and treats it
// as if it were UTC everywhere (Scheduler, rtc_sync -- see
// Ds1307Clock::syncFromUnix). So configTime() is given offset 0 (TZ stays
// UTC, localtime_r/mktime keep behaving as before) and the IST offset is
// added here, exactly as the app's rtc_sync does on the phone.
class NtpSync {
public:
  void loop(bool staConnected, Ds1307Clock& clk) {
    uint32_t now = millis();
    if (!staConnected) { _waiting = false; return; }

    if (!_waiting) {
      bool due = (_lastAttempt == 0) ||
                 (_synced ? (now - _lastSync > RESYNC_MS) : (now - _lastAttempt > RETRY_MS));
      if (!due) return;
      configTime(0, 0, "in.pool.ntp.org", "asia.pool.ntp.org", "pool.ntp.org");
      Serial.println("[NTP] Syncing time...");
      _waiting = true;
      _start = now;
      _lastAttempt = now;
      return;
    }

    time_t utc = time(nullptr);
    if (utc > 1700000000) {   // the system clock has been set by SNTP
      _waiting = false;
      _synced = true;
      _lastSync = now;
      time_t wall = utc + IST_OFFSET_SEC;
      long drift = (long)(wall - clk.now());
      bool rtcWasOk = clk.isRunning();
      if (!rtcWasOk || drift > 2 || drift < -2) {
        clk.syncFromUnix((uint32_t)wall);   // also starts a halted DS1307
        Serial.printf("[NTP] RTC set from internet time (was %s, off by %lds)\n",
                      rtcWasOk ? "running" : "not set", drift);
      } else {
        Serial.printf("[NTP] RTC already correct (off by %lds)\n", drift);
      }
    } else if (now - _start > ATTEMPT_MS) {
      _waiting = false;
      Serial.println("[NTP] No answer — keeping the RTC time, will retry");
    }
  }

private:
  static constexpr long     IST_OFFSET_SEC = 19800;              // UTC+5:30
  static constexpr uint32_t ATTEMPT_MS     = 15000;
  static constexpr uint32_t RETRY_MS       = 60000;              // until the first success
  static constexpr uint32_t RESYNC_MS      = 6UL * 3600UL * 1000UL;
  bool     _waiting = false, _synced = false;
  uint32_t _start = 0, _lastAttempt = 0, _lastSync = 0;
};
