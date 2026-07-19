#pragma once
#include "Config.h"
#include <Arduino.h>
#include "NVSManager.h"
#include "RTCManager.h"
#include "FlowSensor.h"
#include "RelayControl.h"

enum CycleStopReason { STOP_COMPLETED, STOP_USER, STOP_PAUSED, STOP_POWER_RECOVERY };

class Scheduler {
public:
    void begin(NVSManager* nvs, RTCManager* rtc, FlowSensor* flow, RelayControl* relay);
    // Reloads just the cycle list from NVS (after set_cycles saves new
    // ones) — unlike begin(), does NOT touch _state, so it's safe to call
    // while a manual run or scheduled cycle is active/paused. begin()
    // itself does a memset(&_state, 0, ...) with no relay->off() call
    // first, which — if called while something was actually running —
    // orphans the relay physically ON with the scheduler now believing
    // nothing is active, so stopCycle() (manual_off) becomes a silent
    // no-op. Real bug found in testing; this is the fix, not begin()
    // reuse for this purpose.
    void reloadCycles();
    void loop();   // call every loop() iteration

    // Called from MQTT/BLE command handlers
    void startManual(float liters = 0);
    void stopCycle(CycleStopReason reason = STOP_USER);
    void pauseCycle();
    void resumeCycle();

    bool isRunning();
    bool isPaused();
    RunningState getCurrentState();

    // Called on boot for power recovery
    void checkPowerRecovery();

private:
    void _startCycle(Cycle& c, bool isRecovery = false);
    void _checkSchedule();
    void _checkCycleCompletion();
    void _saveProgress();

    NVSManager*   _nvs;
    RTCManager*   _rtc;
    FlowSensor*   _flow;
    RelayControl* _relay;

    Cycle        _cycles[MAX_CYCLES];
    uint8_t      _cycleCount = 0;
    RunningState _state;

    uint32_t _lastScheduleCheck = 0;
    uint32_t _lastStateSave     = 0;
    uint32_t _lastMinuteTick    = 0;
    // Target for the current manual run (0 = no target, run until
    // explicitly stopped). Previously this was tracked via a static Cycle
    // local to startManual() that _checkCycleCompletion() never actually
    // read — manual liter-target runs could never auto-stop as a result.
    float _manualTarget = 0;
};
