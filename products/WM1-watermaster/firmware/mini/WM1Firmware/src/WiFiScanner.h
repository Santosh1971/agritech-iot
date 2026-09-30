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
//
// v2 (confirmed on the bench, 30 Sep 2026): the core's scanComplete()
// reports WIFI_SCAN_FAILED once 20 x max_ms_per_chan has passed — a
// fixed guess, NOT the driver's verdict. Under AP+STA with a phone on
// the SoftAP a real scan runs right up to (or past) that guess, so
// "failed" usually meant "still scanning". The old retry then started
// a new scan on top of it, which aborts the running one and posts a
// bogus SCAN_DONE with 0 results — exactly the "Scan failed — retrying
// (1/5)... Scan found 0 networks" pair seen in the log. Now a FAILED
// from the core is treated as "keep waiting" until our own ceiling;
// only then is the driver scan explicitly stopped, and the retry is
// delayed so the stop's own SCAN_DONE can't be mistaken for a result.
class WiFiScanner {
public:
    void   startScan();
    bool   checkComplete();
    String resultAsJson();
private:
    void _begin();
    int _lastFound = WIFI_SCAN_RUNNING;
    int _retries = 0;
    bool _zeroRetried = false;
    bool _startRejected = false;
    uint32_t _scanStartMillis = 0;
    uint32_t _restartAt = 0;          // non-zero = stopped, waiting to restart
    static const int MAX_RETRIES = 2;
    static constexpr uint32_t SCAN_MS_PER_CHAN = 300;  // core's own timeout = 20x this = 6s
    static constexpr uint32_t MAX_SCAN_MS = 15000;     // our ceiling per attempt
    static constexpr uint32_t REJECTED_RETRY_MS = 1000;
    static constexpr uint32_t RESTART_DELAY_MS = 500;
};
