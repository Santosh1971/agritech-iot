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
//
// Separately, confirmed live: once STA is actually CONNECTED to a
// router (not just SoftAP-only), a scan can sit in WIFI_SCAN_RUNNING
// forever and never resolve to either a result or WIFI_SCAN_FAILED at
// all — a known ESP32 Arduino-core limitation scanning while
// associated, not something the retry loop above can catch since it
// only fires on an explicit FAILED result. _maxScanMillis below is a
// hard ceiling so a scan can never hang the wifi_scan feature
// indefinitely regardless of why the driver never resolves it.
class WiFiScanner {
public:
    void   startScan();
    bool   checkComplete();
    String resultAsJson();
private:
    int _lastFound = WIFI_SCAN_RUNNING;
    int _retries = 0;
    uint32_t _scanStartMillis = 0;
    static const int MAX_RETRIES = 5;
    static constexpr uint32_t MAX_SCAN_MS = 12000;
};
