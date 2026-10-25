#include "irrigation_core.h"

#include <math.h>

namespace wm2core {

static float valveLitres(const Cfg& c, const float* perInput, int v) {
  float s = 0;
  for (int i = 0; i < NUM_FLOW; i++)
    if (c.flowValve[i] == v) s += perInput[i];
  return s;
}

void Core::_push(const Event& e) {
  if (_evn < MAX_EVENTS) _ev[_evn++] = e;
}

void Core::_setOpen(int v, bool open, uint8_t reason) {
  ValveState& s = _vs[v];
  if (s.open == open) return;
  if (!open) {
    // Finish a learning sample for this valve's sensors.
    for (int i = 0; i < NUM_FLOW; i++) {
      if (cfg.flowValve[i] != v) continue;
      if (_learnSec[i] >= 60) {
        float avg = _learnSum[i] / _learnSec[i];
        _normal[i] = _samples[i] == 0 ? avg : _normal[i] * 0.7f + avg * 0.3f;
        if (_samples[i] < 255) _samples[i]++;
      }
      _learnSum[i] = 0;
      _learnSec[i] = 0;
    }
    s.openSec = 0;
  }
  s.open = open;
  Event e;
  e.type = EV_VALVE;
  e.valve = (int8_t)v;
  e.a = open ? 1 : 0;
  e.reason = reason;
  _push(e);
}

bool Core::programmedOn(int v, uint32_t day) const {
  const Program& p = cfg.v[v].prog;
  if (!p.enabled) return false;
  if (p.everyDays > 0) {
    if (day < p.anchorDay) return false;
    return ((day - p.anchorDay) % p.everyDays) == 0;
  }
  return (p.daysMask >> weekday(day)) & 1;
}

bool Core::_inWindow(const Program& p, uint32_t localEpoch) const {
  uint16_t m = (uint16_t)((localEpoch % 86400u) / 60u);
  if (p.winFrom == p.winTo) return true;
  if (p.winFrom < p.winTo) return m >= p.winFrom && m < p.winTo;
  return m >= p.winFrom || m < p.winTo;  // wraps midnight
}

bool Core::_programWants(int v, uint32_t localEpoch) const {
  const ValveState& s = _vs[v];
  const Program& p = cfg.v[v].prog;
  if (!p.enabled || s.done) return false;
  bool today = programmedOn(v, _day) || s.carrySec > 0 || s.carryL > 0;
  return today && _inWindow(p, localEpoch);
}

void Core::_rollDay(uint32_t newDay) {
  for (int v = 0; v < NUM_VALVES; v++) {
    ValveState& s = _vs[v];
    const ValveCfg& c = cfg.v[v];
    bool wasDue = c.mode == MODE_PROGRAM &&
                  (programmedOn(v, _day) || s.carrySec > 0 || s.carryL > 0);
    uint32_t targetSec = (uint32_t)c.prog.minutes * 60u + s.carrySec;
    float targetL = (float)c.prog.litres + s.carryL;
    if (wasDue) {
      Event e;
      e.type = EV_DAY_RESULT;
      e.valve = (int8_t)v;
      DayResult& r = e.day;
      r.day = _day;
      if (c.prog.type == PROG_TIMER) r.plannedMin = (uint16_t)(targetSec / 60u);
      else r.plannedL = (uint32_t)targetL;
      r.runMin = (uint16_t)(s.runSec / 60u);
      int k = 0;
      for (int i = 0; i < NUM_FLOW && k < 2; i++) {
        if (cfg.flowValve[i] != v) continue;
        r.litres[k] = _dayL[i];
        r.normalL[k] = _normal[i] * (s.runSec / 60.0f);
        k++;
      }
      r.waterMin = (uint16_t)(_waterSec / 60u);
      if (s.done) r.status = 0;
      else if (_waterSec == 0) r.status = 2;
      else r.status = 1;
      _push(e);
    }
    // Carry the shortfall to the next day if asked.
    s.carrySec = 0;
    s.carryL = 0;
    if (wasDue && c.prog.carryOver && !s.done) {
      if (c.prog.type == PROG_TIMER && s.runSec < targetSec) s.carrySec = targetSec - s.runSec;
      if (c.prog.type == PROG_LITRES && s.runL < targetL) s.carryL = targetL - s.runL;
    }
    s.runSec = 0;
    s.runL = 0;
    s.done = false;
  }
  for (int i = 0; i < NUM_FLOW; i++) _dayL[i] = 0;
  _waterSec = 0;
  _day = newDay;
}

void Core::_learn(const Inputs& in, float dt) {
  for (int i = 0; i < NUM_FLOW; i++) {
    int v = cfg.flowValve[i];
    if (v < 0) continue;
    const ValveState& s = _vs[v];
    // Steady flow only: valve open with water for 2+ minutes, not on bypass alone.
    if (s.open && in.ps && s.openSec >= 120) {
      _learnSum[i] += in.lpm[i] * dt;
      _learnSec[i] += dt;
    }
  }
}

void Core::_alarms(const Inputs& in, float dt) {
  bool cond[A_COUNT] = {};
  int8_t cv[A_COUNT] = {-1, -1, -1, -1, -1};
  bool anyOpen = false;
  float flowSum = 0;
  for (int i = 0; i < NUM_FLOW; i++) flowSum += in.lpm[i];

  for (int v = 0; v < NUM_VALVES; v++) {
    const ValveState& s = _vs[v];
    if (s.open) anyOpen = true;
    if (!s.open || !_water || s.openSec < 120) continue;
    float lpm = 0, norm = 0;
    int ins[2] = {-1, -1};
    int k = 0;
    bool learned = true;
    for (int i = 0; i < NUM_FLOW; i++) {
      if (cfg.flowValve[i] != v) continue;
      lpm += in.lpm[i];
      norm += _normal[i];
      if (_samples[i] < 3) learned = false;
      if (k < 2) ins[k++] = i;
    }
    if (!learned || norm <= 0) continue;
    float pct = lpm * 100.0f / norm;
    if (pct < cfg.lowPct) { cond[A_LOW_FLOW] = true; cv[A_LOW_FLOW] = (int8_t)v; }
    if (pct > cfg.highPct) { cond[A_HIGH_FLOW] = true; cv[A_HIGH_FLOW] = (int8_t)v; }
    if (k == 2 && _normal[ins[0]] > 0 && _normal[ins[1]] > 0) {
      float ra = in.lpm[ins[0]] / _normal[ins[0]];
      float rb = in.lpm[ins[1]] / _normal[ins[1]];
      float hi = ra > rb ? ra : rb;
      if (hi > 0 && fabsf(ra - rb) * 100.0f / hi > cfg.imbalancePct) {
        cond[A_IMBALANCE] = true;
        cv[A_IMBALANCE] = (int8_t)v;
      }
    }
  }
  if (!anyOpen && flowSum > cfg.flowSeenLpm) cond[A_LEAK] = true;

  // Pressure switch fault: flow keeps coming but the switch says no pressure.
  bool flowSeen = flowSum > cfg.flowSeenLpm;
  if (flowSeen && !in.ps) _psFaultSec += dt;
  else _psFaultSec = 0;
  cond[A_PS_FAULT] = _psFaultSec >= cfg.psFaultMin * 60.0f;

  for (int a = 0; a < A_COUNT; a++) {
    float delay = a == A_PS_FAULT ? 0 : cfg.alarmDelayS;
    if (cond[a]) {
      _alarmTimer[a] += dt;
      if (!_alarm[a] && _alarmTimer[a] >= delay) {
        _alarm[a] = true;
        _alarmValve[a] = cv[a];
        Event e;
        e.type = EV_ALARM;
        e.valve = cv[a];
        e.a = 1;
        e.reason = (uint8_t)a;
        _push(e);
      }
    } else {
      _alarmTimer[a] = 0;
      if (_alarm[a]) {
        _alarm[a] = false;
        Event e;
        e.type = EV_ALARM;
        e.valve = _alarmValve[a];
        e.a = 0;
        e.reason = (uint8_t)a;
        _push(e);
      }
    }
  }
}

int Core::tick(const Inputs& in) {
  _evn = 0;
  float dt = in.dtMs / 1000.0f;

  // Day bookkeeping first, so progress lands on the right day.
  if (in.timeValid) {
    uint32_t d = localDay(in.localEpoch, cfg.dayStartHour);
    if (!_started) {
      _day = d;
      _started = true;
    } else if (d != _day) {
      _rollDay(d);
    }
  }

  bool manualDone[NUM_VALVES] = {false, false};
  // 1) Account for the interval that just passed, with last tick's valve states.
  for (int i = 0; i < NUM_FLOW; i++) _dayL[i] += in.litresDelta[i];
  if (_water) _waterSec += (uint32_t)(dt + 0.5f);
  for (int v = 0; v < NUM_VALVES; v++) {
    ValveState& s = _vs[v];
    const ValveCfg& c = cfg.v[v];
    bool flowing = s.open && (_water || in.bypass);
    float l = valveLitres(cfg, in.litresDelta, v);
    if (s.open && _water) s.openSec += (uint32_t)(dt + 0.5f);
    if (!flowing) continue;
    if (s.manual) {
      if (s.manualTimed) s.manualLeftSec = s.manualLeftSec > dt ? s.manualLeftSec - (uint32_t)(dt + 0.5f) : 0;
      if (s.manualLitres) s.manualLeftL = s.manualLeftL > l ? s.manualLeftL - l : 0;
      if ((s.manualTimed && s.manualLeftSec == 0) || (s.manualLitres && s.manualLeftL <= 0)) {
        s.manual = false;
        manualDone[v] = true;
        Event e;
        e.type = EV_MANUAL_DONE;
        e.valve = (int8_t)v;
        _push(e);
      }
    } else if (c.mode == MODE_PROGRAM && c.prog.enabled && !s.done) {
      s.runSec += (uint32_t)(dt + 0.5f);
      s.runL += l;
      if (c.prog.type == PROG_TIMER && s.runSec >= (uint32_t)c.prog.minutes * 60u + s.carrySec) s.done = true;
      if (c.prog.type == PROG_LITRES && s.runL >= (float)c.prog.litres + s.carryL) s.done = true;
    }
  }

  // 2) Water present?
  float flowSum = 0;
  for (int i = 0; i < NUM_FLOW; i++) flowSum += in.lpm[i];
  bool water = in.ps || flowSum > cfg.flowSeenLpm;
  if (water != _water) {
    _water = water;
    Event e;
    e.type = EV_WATER;
    e.a = water ? 1 : 0;
    _push(e);
  }

  // 3) What does each valve want?
  bool want[NUM_VALVES];
  uint8_t why[NUM_VALVES];
  for (int v = 0; v < NUM_VALVES; v++) {
    ValveState& s = _vs[v];
    const ValveCfg& c = cfg.v[v];
    want[v] = false;
    why[v] = R_NONE;
    if (c.neverClose) {
      want[v] = true;
      why[v] = R_NEVER_CLOSE;
    } else if (c.mode == MODE_OFF) {
      s.manual = false;
      why[v] = R_MODE_OFF;
    } else if (s.manual) {
      want[v] = true;
      why[v] = R_MANUAL;
    } else if (c.mode == MODE_PROGRAM && in.timeValid && _programWants(v, in.localEpoch)) {
      want[v] = true;
      why[v] = R_PROGRAM;
    } else if (c.mode == MODE_PROGRAM && s.done) {
      why[v] = R_PROGRAM_DONE;
    } else {
      why[v] = manualDone[v] ? R_MANUAL_DONE : R_WINDOW_END;
    }
    s.due = c.mode == MODE_PROGRAM && in.timeValid && !s.done &&
            (programmedOn(v, _day) || s.carrySec > 0 || s.carryL > 0);
  }

  // One after the other: program runs only (manual is an explicit user choice).
  bool seqWait[NUM_VALVES] = {false, false};
  if (cfg.sequential && want[0] && want[1] && why[0] == R_PROGRAM && why[1] == R_PROGRAM) {
    int keep = _vs[1].open && !_vs[0].open ? 1 : 0;
    int other = 1 - keep;
    want[other] = false;
    seqWait[other] = true;
  }

  // 4) Apply with the water interlock.
  bool canFlow = _water || in.bypass;
  for (int v = 0; v < NUM_VALVES; v++) {
    ValveState& s = _vs[v];
    const ValveCfg& c = cfg.v[v];
    if (want[v]) {
      if (s.open) {
        if (!canFlow && !c.neverClose) {
          s.noWaterSec += (uint32_t)(dt + 0.5f);
          if (c.closeOnWaterLoss && s.noWaterSec >= (uint32_t)c.waterLossCloseMin * 60u) {
            _setOpen(v, false, R_WATER_LOSS);
            s.waiting = true;
          }
        } else {
          s.noWaterSec = 0;
        }
      } else if (canFlow || c.neverClose) {
        _setOpen(v, true, s.waiting && s.noWaterSec > 0 ? R_WATER_BACK : why[v]);
        s.waiting = false;
        s.noWaterSec = 0;
      } else {
        s.waiting = true;
      }
    } else {
      s.waiting = seqWait[v];
      s.noWaterSec = 0;
      if (s.open) _setOpen(v, false, seqWait[v] ? R_SEQUENCE : why[v]);
    }
  }

  _learn(in, dt);
  _alarms(in, dt);
  return _evn;
}

bool Core::manualOpen(int v, uint32_t seconds, float litres) {
  if (v < 0 || v >= NUM_VALVES) { lastError = "bad valve"; return false; }
  if (cfg.v[v].mode == MODE_OFF) { lastError = "valve is off"; return false; }
  ValveState& s = _vs[v];
  s.manual = true;
  s.manualTimed = seconds > 0;
  s.manualLeftSec = seconds;
  s.manualLitres = litres > 0;
  s.manualLeftL = litres;
  return true;
}

bool Core::manualClose(int v) {
  if (v < 0 || v >= NUM_VALVES) { lastError = "bad valve"; return false; }
  if (cfg.v[v].neverClose) { lastError = "valve is set never-close"; return false; }
  ValveState& s = _vs[v];
  s.manual = false;
  // Closing by hand also stops the rest of today's program for this valve.
  if (cfg.v[v].mode == MODE_PROGRAM && s.due) s.done = true;
  return true;
}

void Core::setMode(int v, Mode m) {
  if (v < 0 || v >= NUM_VALVES) return;
  cfg.v[v].mode = m;
  if (m == MODE_OFF) _vs[v].manual = false;
}

void Core::setProgram(int v, const Program& p) {
  if (v < 0 || v >= NUM_VALVES) return;
  cfg.v[v].prog = p;
  // A new program starts fresh for today.
  _vs[v].done = false;
  _vs[v].carrySec = 0;
  _vs[v].carryL = 0;
}

void Core::restoreProgress(int v, uint32_t day, uint32_t runSec, float runL) {
  if (v < 0 || v >= NUM_VALVES) return;
  _day = day;
  _started = true;
  _vs[v].runSec = runSec;
  _vs[v].runL = runL;
  const Program& p = cfg.v[v].prog;
  if (p.enabled && ((p.type == PROG_TIMER && runSec >= (uint32_t)p.minutes * 60u) ||
                    (p.type == PROG_LITRES && runL >= (float)p.litres)))
    _vs[v].done = true;
}

const char* reasonName(uint8_t r) {
  switch (r) {
    case R_PROGRAM: return "program";
    case R_PROGRAM_DONE: return "program_done";
    case R_MANUAL: return "manual";
    case R_MANUAL_DONE: return "manual_done";
    case R_WATER_LOSS: return "water_loss";
    case R_WATER_BACK: return "water_back";
    case R_MODE_OFF: return "mode_off";
    case R_SEQUENCE: return "sequence";
    case R_NEVER_CLOSE: return "never_close";
    case R_WINDOW_END: return "stopped";
    case R_DAY_END: return "day_end";
    default: return "none";
  }
}

const char* alarmName(uint8_t a) {
  switch (a) {
    case A_LOW_FLOW: return "low_flow";
    case A_HIGH_FLOW: return "high_flow";
    case A_IMBALANCE: return "imbalance";
    case A_LEAK: return "leak";
    case A_PS_FAULT: return "ps_fault";
    default: return "unknown";
  }
}

}  // namespace wm2core
