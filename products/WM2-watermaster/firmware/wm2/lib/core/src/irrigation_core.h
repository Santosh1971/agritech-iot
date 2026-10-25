#pragma once
// WM2 irrigation logic: decides which valve should be open, counts program
// progress, and raises events/alarms. Pure C++ (no Arduino), so it runs in
// the native unit tests on a PC as well as on the ESP32.
//
// Rules implemented here come from the WM2 Girish Farm spec:
//  - a valve opens only when water is present (pressure switch or flow),
//    unless bypass is on
//  - day-wise programs: run minutes (or litres) on programmed days; time
//    without water does not count, so a power cut just pauses the run
//  - water lost while open: keep open, or close after N minutes (per valve)
//  - V1/V2 together or one after the other
//  - day result per programmed valve at the day boundary
//  - litres feedback: learned normal flow -> low / high / imbalance / leak

#include <stdint.h>

namespace wm2core {

constexpr int NUM_VALVES = 2;
constexpr int NUM_FLOW = 4;

enum Mode : uint8_t { MODE_PROGRAM = 0, MODE_MANUAL = 1, MODE_OFF = 2 };
enum ProgType : uint8_t { PROG_TIMER = 0, PROG_LITRES = 1 };

struct Program {
  bool enabled = false;
  ProgType type = PROG_TIMER;
  uint8_t daysMask = 0;      // bit0 = Mon ... bit6 = Sun
  uint8_t everyDays = 0;     // >0: every N days counted from anchorDay (daysMask ignored)
  uint32_t anchorDay = 0;    // local day index the every-N count starts from
  uint16_t minutes = 0;      // PROG_TIMER target
  uint32_t litres = 0;       // PROG_LITRES target
  uint16_t winFrom = 0;      // minutes of local day, window start
  uint16_t winTo = 1440;     // window end (exclusive); from > to wraps midnight
  bool carryOver = false;    // unfinished minutes/litres move to the next day
};

struct ValveCfg {
  Mode mode = MODE_PROGRAM;
  Program prog;
  bool closeOnWaterLoss = false;   // false = keep open when water goes away
  uint16_t waterLossCloseMin = 10;
  bool neverClose = false;         // relief valve until a pump relief valve is fitted
};

struct Cfg {
  ValveCfg v[NUM_VALVES];
  int8_t flowValve[NUM_FLOW] = {0, 0, 1, 1};  // flow input -> valve index, -1 unused
  bool sequential = true;         // V1 then V2 instead of together
  uint8_t dayStartHour = 0;       // day boundary for programs/results (local hour)
  float flowSeenLpm = 0.5f;       // above this on any input counts as "water present"
  uint8_t lowPct = 50;            // alarm below this % of learned normal
  uint8_t highPct = 150;          // alarm above this %
  uint8_t imbalancePct = 40;      // two sensors of one valve differ by more than this %
  uint16_t alarmDelayS = 180;     // condition must hold this long
  uint16_t psFaultMin = 5;        // flow without pressure switch this long -> ps_fault
};

struct Inputs {
  uint32_t localEpoch = 0;        // local wall-clock seconds (UTC epoch + tz offset)
  bool timeValid = false;         // false: programs do not run
  bool ps = false;                // debounced pressure switch (true = pressure)
  bool bypass = false;
  float lpm[NUM_FLOW] = {0, 0, 0, 0};
  float litresDelta[NUM_FLOW] = {0, 0, 0, 0};  // since previous tick
  uint32_t dtMs = 1000;           // since previous tick
};

enum EventType : uint8_t {
  EV_NONE = 0,
  EV_VALVE,        // a = open(1)/close(0), reason
  EV_WATER,        // a = present
  EV_DAY_RESULT,   // valve, uses DayResult
  EV_ALARM,        // a = active, code
  EV_MANUAL_DONE,
};

enum Reason : uint8_t {
  R_NONE = 0, R_PROGRAM, R_PROGRAM_DONE, R_MANUAL, R_MANUAL_DONE, R_WATER_LOSS,
  R_WATER_BACK, R_MODE_OFF, R_SEQUENCE, R_NEVER_CLOSE, R_WINDOW_END, R_DAY_END,
};

enum AlarmCode : uint8_t {
  A_LOW_FLOW = 0, A_HIGH_FLOW, A_IMBALANCE, A_LEAK, A_PS_FAULT, A_COUNT
};

struct DayResult {
  uint32_t day = 0;          // local day index (after dayStartHour shift)
  uint16_t plannedMin = 0;   // timer target incl. carry (0 for litres programs)
  uint32_t plannedL = 0;     // litres target incl. carry (0 for timer)
  uint16_t runMin = 0;
  float litres[2] = {0, 0};  // per flow sensor of this valve (first two mapped)
  float normalL[2] = {0, 0}; // expected litres for runMin at learned normal flow
  uint16_t waterMin = 0;     // minutes with water present that day
  uint8_t status = 0;        // 0 done, 1 partial, 2 no_water
};

struct Event {
  EventType type = EV_NONE;
  int8_t valve = -1;
  uint8_t a = 0;
  uint8_t reason = R_NONE;
  DayResult day;
};

// Live per-valve state, readable for status/telemetry.
struct ValveState {
  bool open = false;          // desired state (what the node commands)
  bool waiting = false;       // wants to open but no water / sequence
  bool due = false;           // programmed today and not finished
  bool done = false;          // today's program finished
  uint32_t runSec = 0;        // today's program run time (counted only with water)
  float runL = 0;             // today's program litres
  uint32_t carrySec = 0;      // carried in from yesterday
  float carryL = 0;
  // manual run
  bool manual = false;
  uint32_t manualLeftSec = 0; // 0 with manual && manualLeftL==0 means open-ended
  float manualLeftL = 0;
  bool manualTimed = false;
  bool manualLitres = false;
  // bookkeeping
  uint32_t noWaterSec = 0;
  uint32_t openSec = 0;       // continuous open time with water (for learning)
};

constexpr int MAX_EVENTS = 16;

class Core {
 public:
  Cfg cfg;

  // Advance one tick. Returns number of events written to `events`.
  int tick(const Inputs& in);

  const Event* events() const { return _ev; }
  const ValveState& valve(int i) const { return _vs[i]; }
  bool water() const { return _water; }
  uint32_t waterSecToday() const { return _waterSec; }
  bool alarm(AlarmCode c) const { return _alarm[c]; }
  float normalLpm(int input) const { return _normal[input]; }
  uint8_t normalSamples(int input) const { return _samples[input]; }
  uint32_t dayIndex() const { return _day; }

  // Commands. Return false (with reason in lastError) if refused.
  bool manualOpen(int v, uint32_t seconds, float litres);
  bool manualClose(int v);
  void setMode(int v, Mode m);
  void setProgram(int v, const Program& p);
  void setNormal(int input, float lpm, uint8_t samples) { _normal[input] = lpm; _samples[input] = samples; }
  // Restore today's progress after a reboot (from persisted state).
  void restoreProgress(int v, uint32_t day, uint32_t runSec, float runL);
  const char* lastError = "";

  static uint32_t localDay(uint32_t localEpoch, uint8_t dayStartHour) {
    uint32_t t = localEpoch >= (uint32_t)dayStartHour * 3600u ? localEpoch - dayStartHour * 3600u : 0;
    return t / 86400u;
  }
  static uint8_t weekday(uint32_t day) { return (uint8_t)((day + 3) % 7); }  // 0 = Mon (1970-01-01 was Thu)
  bool programmedOn(int v, uint32_t day) const;

 private:
  void _push(const Event& e);
  void _setOpen(int v, bool open, uint8_t reason);
  void _rollDay(uint32_t newDay);
  bool _inWindow(const Program& p, uint32_t localEpoch) const;
  bool _programWants(int v, uint32_t localEpoch) const;
  void _alarms(const Inputs& in, float dt);
  void _learn(const Inputs& in, float dt);

  ValveState _vs[NUM_VALVES];
  Event _ev[MAX_EVENTS];
  int _evn = 0;
  bool _water = false;
  bool _started = false;
  uint32_t _day = 0;
  uint32_t _waterSec = 0;
  float _normal[NUM_FLOW] = {0, 0, 0, 0};
  uint8_t _samples[NUM_FLOW] = {0, 0, 0, 0};
  float _learnSum[NUM_FLOW] = {0, 0, 0, 0};
  float _learnSec[NUM_FLOW] = {0, 0, 0, 0};
  bool _alarm[A_COUNT] = {};
  float _alarmTimer[A_COUNT] = {};
  int8_t _alarmValve[A_COUNT] = {-1, -1, -1, -1, -1};
  float _psFaultSec = 0;
  float _dayL[NUM_FLOW] = {0, 0, 0, 0};
};

const char* reasonName(uint8_t r);
const char* alarmName(uint8_t a);

}  // namespace wm2core
