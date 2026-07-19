#pragma once
#include "Config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "Config.h"

// ---------- Cycle struct ----------
enum OperationMode { LITER_BASED, TIME_BASED, TIME_WINDOW_LITER };

struct Cycle {
    uint8_t  id;
    char     name[32];
    uint8_t  startHour;
    uint8_t  startMinute;
    uint8_t  endHour;
    uint8_t  endMinute;
    OperationMode mode;
    float    targetLiters;
    bool     enabled;
};

// ---------- Running state (survives power loss) ----------
struct RunningState {
    bool     active;
    bool     paused;
    uint8_t  cycleId;
    float    litersDelivered;
    uint32_t startUnix;
    char     startedBy[16];   // "auto" or "manual"
};

// ---------- History entry ----------
struct HistoryEntry {
    uint32_t timestamp;
    uint8_t  cycleId;
    char     cycleName[32];
    OperationMode mode;
    float    litersDelivered;
    uint16_t durationSeconds;
    char     status[16];     // "completed","paused","interrupted","manual"
};

class NVSManager {
public:
    void begin();

    // Cycles
    void saveCycles(Cycle* cycles, uint8_t count);
    uint8_t loadCycles(Cycle* cycles);

    // WiFi / MQTT credentials
    void saveWiFi(const char* ssid, const char* pass);
    bool loadWiFi(char* ssid, char* pass);

    void saveForcedLocalMode(bool forced);
    bool loadForcedLocalMode();
    void saveMQTT(const char* broker, uint16_t port, const char* user, const char* pass);
    bool loadMQTT(char* broker, uint16_t& port, char* user, char* pass);

    // Running state
    void saveRunningState(const RunningState& state);
    bool loadRunningState(RunningState& state);
    void clearRunningState();
    void clearHistory();

    // History
    void addHistoryEntry(const HistoryEntry& entry);
    uint8_t getHistory(HistoryEntry* entries, uint8_t maxCount);
    uint8_t getHistoryInRange(HistoryEntry* entries, uint8_t maxCount,
                               uint32_t fromTs, uint32_t toTs);

    // Calibration
    void saveCalibration(uint32_t pulsesPerLiter);
    uint32_t loadCalibration();

    // Factory reset
    void factoryReset();

private:
    Preferences _prefs;
    // Shared scratch buffer for the packed history blob (see .cpp) — one
    // member-owned buffer rather than a function-local static in each of
    // addHistoryEntry/getHistory/getHistoryInRange, since all three need
    // the same ~12.5KB working set and there's no benefit to duplicating it.
    HistoryEntry _historyBuf[HISTORY_MAX_ENTRIES];
    void _loadHistoryBlob();
    void _saveHistoryBlob();
};
