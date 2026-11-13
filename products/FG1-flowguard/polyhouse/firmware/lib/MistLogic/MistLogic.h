#pragma once
// Misting decision logic — plain C++ (no Arduino), unit-tested on the host
// with `pio test -e native`. The controller feeds it the control T/RH, the
// time of day and a monotonic seconds clock once a second; it answers
// whether the pump relay should be ON and why.
//
// Customer rules (Belgaum rose polyhouse):
//   STOP  (always wins):  T <= tStop (29)  or  RH >= rhHigh (60)
//   START:                T >= tStart (32) or (RH <= rhLow (45) and T > tStop)
//   Between the two (29 < T < 32, 45 < RH < 60) the last decision holds,
//   so the pump does not chatter around one threshold.
//   After an RH stop, misting may restart only once RH <= rhHigh - 3 (57),
//   so humidity hovering at 60 % cannot start and stop the pump repeatedly.
//   While misting is wanted, the pump pulses onSec ON / offSec OFF (2/3 min),
//   only inside the working window (11:00-15:00), all adjustable.
#include <stdint.h>

namespace mist {

enum class Mode : uint8_t { Auto = 0, Manual = 1, Off = 2 };

enum class Reason : uint8_t {
    Idle = 0,          // conditions fine, nothing to do
    PulseOn,           // misting: pump ON part of the pulse
    PulseGap,          // misting: OFF part of the pulse
    StopTempLow,       // T <= tStop
    StopRhHigh,        // RH >= rhHigh
    OutsideWindow,     // outside the working hours
    SensorFault,       // no fresh reading from the control sensor(s)
    StartLimit,        // max starts per hour reached, waiting
    ModeOff,           // user switched misting off
    ManualOn,
    ManualOff,
};
const char* reasonText(Reason r);
const char* modeText(Mode m);

struct Settings {
    float    tStart       = 32.0f;
    float    tStop        = 29.0f;
    float    rhLow        = 45.0f;
    float    rhHigh       = 60.0f;
    uint16_t onSec        = 120;
    uint16_t offSec       = 180;   // also the minimum OFF time between any two starts
    bool     windowOn     = true;
    uint16_t winStartMin  = 11 * 60;
    uint16_t winEndMin    = 15 * 60;
    uint8_t  maxStartsHr  = 15;    // 0 = no limit
    uint16_t manualMaxMin = 30;    // manual ON auto-stops after this (0 = never)

    // Clamps nonsense from the app into a safe range; returns false if it
    // had to change anything.
    bool sanitize();
};

struct Inputs {
    bool     sensorOk   = false;
    float    t          = 0;
    float    rh         = 0;
    bool     timeValid  = false;  // RTC set? if not, the window is ignored
    uint16_t minuteOfDay = 0;     // 0..1439
    uint32_t nowSec     = 0;      // monotonic seconds (millis()/1000)
};

class Controller {
public:
    static constexpr float RH_RESTART_MARGIN = 3.0f;   // %RH below rhHigh before misting may restart

    void setSettings(const Settings& s) { _s = s; }
    const Settings& settings() const { return _s; }

    void setMode(Mode m, uint32_t nowSec);
    Mode mode() const { return _mode; }
    void manualOn(uint32_t nowSec);   // switches to Manual + pump ON
    void manualOff(uint32_t nowSec);  // switches to Manual + pump OFF

    // Call about once a second. Returns the wanted relay state.
    bool update(const Inputs& in);

    bool     pumpOn() const        { return _pumpOn; }
    bool     demand() const        { return _demand; }
    Reason   reason() const        { return _reason; }
    bool     inWindow() const      { return _inWindow; }
    // Seconds until the current ON/OFF phase ends (0 when not pulsing).
    uint32_t phaseRemaining() const { return _phaseRemaining; }
    uint8_t  startsLastHour(uint32_t nowSec) const;
    uint32_t lastChangeSec() const { return _lastChange; }

    static bool minuteInWindow(uint16_t m, uint16_t start, uint16_t end);

private:
    void _set(bool on, uint32_t nowSec);

    Settings _s;
    Mode     _mode    = Mode::Auto;
    bool     _manualWant = false;
    uint32_t _manualSince = 0;
    bool     _pumpOn  = false;
    bool     _demand  = false;
    bool     _inWindow = false;
    Reason   _reason  = Reason::Idle;
    bool     _rhStopped = false;      // latched by RH >= rhHigh, released at rhHigh - RH_RESTART_MARGIN
    bool     _everOff = true;         // boot counts as an OFF edge at t=0: a power blip can't restart the motor before offSec
    uint32_t _onAt    = 0;
    uint32_t _offAt   = 0;
    uint32_t _lastChange = 0;
    uint32_t _phaseRemaining = 0;

    static const uint8_t START_RING = 32;
    uint32_t _starts[START_RING] = {0};
    uint8_t  _startIdx = 0;
    uint8_t  _startCount = 0;
};

// 60-second moving average for a sensor value (samples every few seconds).
class RollingAvg {
public:
    void add(float v, uint32_t nowSec);
    bool valid(uint32_t nowSec, uint32_t maxAgeSec) const;
    float value(uint32_t nowSec) const;    // mean of samples within windowSec
    void clear() { _n = 0; }
    uint32_t windowSec = 60;
private:
    static const uint8_t N = 16;
    float    _v[N];
    uint32_t _ts[N];
    uint8_t  _head = 0;
    uint8_t  _n = 0;
};

} // namespace mist
