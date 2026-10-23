#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Fully non-blocking wrapper around WiFi.scanNetworks(). A blocking scan
// (even in "async" mode polled with delay()) was found to still starve
// the async_tcp background task badly enough to trip its watchdog and
// crash the device — confirmed on real hardware. This class never
// blocks: startScan() kicks off the scan and returns immediately; the
// caller polls checkComplete() from loop() until it's ready, then reads
// resultAsJson() once.
//
// WiFi.scanComplete() returns: >=0 = found count, WIFI_SCAN_RUNNING(-1)
// = still going, WIFI_SCAN_FAILED(-2) = failed outright. A failure is
// common (not rare) under concurrent AP+STA with an active client
// connected — confirmed repeatedly on real hardware — so checkComplete()
// auto-restarts the scan a few times on failure before finally giving
// up, mirroring the retry behavior the original blocking implementation
// always had, just done here without any delay() calls.
class WiFiScanner {
public:
    void   startScan();
    bool   checkComplete();
    String resultAsJson();
private:
    void freeRadioForScan();
    void restoreRadioAfterScan();

    int _lastFound = WIFI_SCAN_RUNNING;
    int _retries = 0;
    uint32_t _startMs = 0;
    uint32_t _lastKickMs = 0;
    bool _autoReconnectSaved = false;
    bool _autoReconnectOverridden = false;
    static const int MAX_RETRIES = 5;
    // A failed scan_start is instant (not a timeout), so unpaced retries
    // all burn in a single loop() pass. Space them out so the STA
    // disconnect issued in freeRadioForScan() has time to take effect.
    static const uint32_t RETRY_BACKOFF_MS = 500;
};
