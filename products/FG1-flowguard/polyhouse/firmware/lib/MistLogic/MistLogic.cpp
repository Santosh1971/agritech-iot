#include "MistLogic.h"

namespace mist {

const char* reasonText(Reason r) {
    switch (r) {
        case Reason::Idle:          return "idle";
        case Reason::PulseOn:       return "misting_on";
        case Reason::PulseGap:      return "misting_gap";
        case Reason::StopTempLow:   return "stop_temp_low";
        case Reason::StopRhHigh:    return "stop_rh_high";
        case Reason::OutsideWindow: return "outside_window";
        case Reason::SensorFault:   return "sensor_fault";
        case Reason::StartLimit:    return "start_limit";
        case Reason::ModeOff:       return "mode_off";
        case Reason::ManualOn:      return "manual_on";
        case Reason::ManualOff:     return "manual_off";
    }
    return "?";
}

const char* modeText(Mode m) {
    switch (m) {
        case Mode::Auto:   return "auto";
        case Mode::Manual: return "manual";
        case Mode::Off:    return "off";
    }
    return "?";
}

static bool clampF(float& v, float lo, float hi) {
    if (v != v) { v = lo; return false; }           // NaN
    if (v < lo) { v = lo; return false; }
    if (v > hi) { v = hi; return false; }
    return true;
}
template <typename T> static bool clampI(T& v, T lo, T hi) {
    if (v < lo) { v = lo; return false; }
    if (v > hi) { v = hi; return false; }
    return true;
}

bool Settings::sanitize() {
    bool ok = true;
    ok &= clampF(tStop, 10, 50);
    ok &= clampF(tStart, 10, 55);
    if (tStart < tStop + 0.5f) { tStart = tStop + 0.5f; ok = false; }
    ok &= clampF(rhLow, 5, 95);
    ok &= clampF(rhHigh, 10, 100);
    if (rhHigh < rhLow + 2) { rhHigh = rhLow + 2; ok = false; }
    ok &= clampI<uint16_t>(onSec, 5, 3600);
    // A DOL-started motor needs a real rest between starts.
    ok &= clampI<uint16_t>(offSec, 30, 7200);
    ok &= clampI<uint16_t>(winStartMin, 0, 1439);
    ok &= clampI<uint16_t>(winEndMin, 0, 1439);
    ok &= clampI<uint8_t>(maxStartsHr, 0, 60);
    ok &= clampI<uint16_t>(manualMaxMin, 0, 240);
    return ok;
}

bool Controller::minuteInWindow(uint16_t m, uint16_t start, uint16_t end) {
    if (start == end) return true;                   // full day
    if (start < end)  return m >= start && m < end;
    return m >= start || m < end;                    // wraps past midnight
}

void Controller::setMode(Mode m, uint32_t nowSec) {
    if (m == Mode::Manual && _mode != Mode::Manual) {
        _manualWant  = _pumpOn;                      // keep the pump as it is
        _manualSince = nowSec;
    }
    _mode = m;
}

void Controller::manualOn(uint32_t nowSec) {
    _mode = Mode::Manual;
    _manualWant = true;
    _manualSince = nowSec;
}

void Controller::manualOff(uint32_t nowSec) {
    _mode = Mode::Manual;
    _manualWant = false;
    _manualSince = nowSec;
}

uint8_t Controller::startsLastHour(uint32_t nowSec) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < _startCount; i++) {
        uint32_t t = _starts[(uint8_t)(_startIdx + START_RING - 1 - i) % START_RING];
        if (nowSec - t < 3600) n++;
    }
    return n;
}

void Controller::_set(bool on, uint32_t nowSec) {
    if (on == _pumpOn) return;
    _pumpOn = on;
    _lastChange = nowSec;
    if (on) {
        _onAt = nowSec;
        _starts[_startIdx] = nowSec;
        _startIdx = (_startIdx + 1) % START_RING;
        if (_startCount < START_RING) _startCount++;
    } else {
        _offAt = nowSec;
        _everOff = true;
    }
}

bool Controller::update(const Inputs& in) {
    const uint32_t now = in.nowSec;
    _phaseRemaining = 0;

    _inWindow = !_s.windowOn || !in.timeValid ||
                minuteInWindow(in.minuteOfDay, _s.winStartMin, _s.winEndMin);

    // Climate demand with hysteresis — evaluated in every mode so the app
    // can show what Auto would do.
    Reason climate = Reason::Idle;
    if (!in.sensorOk) {
        _demand = false;
        climate = Reason::SensorFault;
    } else if (in.rh >= _s.rhHigh || (_rhStopped && in.rh > _s.rhHigh - RH_RESTART_MARGIN)) {
        _rhStopped = true;                    // stays stopped until RH falls 3 % below the stop line
        _demand = false;
        climate = Reason::StopRhHigh;
    } else {
        _rhStopped = false;
        if (in.t <= _s.tStop) {
            _demand = false;
            climate = Reason::StopTempLow;
        } else if (in.t >= _s.tStart || in.rh <= _s.rhLow) {
            _demand = true;                   // T > tStop already true here
        }
        // else: inside the band — _demand unchanged
    }

    if (_mode == Mode::Off) {
        _set(false, now);
        _reason = Reason::ModeOff;
        return _pumpOn;
    }

    if (_mode == Mode::Manual) {
        if (_manualWant && _s.manualMaxMin &&
            now - _manualSince >= (uint32_t)_s.manualMaxMin * 60) {
            // Forgotten manual run: stop and hand back to Auto.
            _manualWant = false;
            _set(false, now);
            _mode = Mode::Auto;
            _reason = Reason::ManualOff;
            return _pumpOn;
        }
        _set(_manualWant, now);
        _reason = _manualWant ? Reason::ManualOn : Reason::ManualOff;
        if (_manualWant && _s.manualMaxMin)
            _phaseRemaining = (uint32_t)_s.manualMaxMin * 60 - (now - _manualSince);
        return _pumpOn;
    }

    // ---- Auto ----
    if (climate != Reason::Idle) {            // a stop rule or sensor fault
        _set(false, now);
        _reason = climate;
        return _pumpOn;
    }
    if (!_inWindow) {
        _set(false, now);
        _reason = Reason::OutsideWindow;
        return _pumpOn;
    }
    if (!_demand) {
        _set(false, now);
        _reason = Reason::Idle;
        return _pumpOn;
    }

    // Misting wanted: pulse.
    if (_pumpOn) {
        uint32_t onFor = now - _onAt;
        if (onFor >= _s.onSec) {
            _set(false, now);
            _reason = Reason::PulseGap;
            _phaseRemaining = _s.offSec;
        } else {
            _reason = Reason::PulseOn;
            _phaseRemaining = _s.onSec - onFor;
        }
        return _pumpOn;
    }

    uint32_t offFor = _everOff ? now - _offAt : 0xFFFFFFFF;
    if (offFor < _s.offSec) {
        _reason = Reason::PulseGap;
        _phaseRemaining = _s.offSec - offFor;
        return _pumpOn;
    }
    if (_s.maxStartsHr && startsLastHour(now) >= _s.maxStartsHr) {
        _reason = Reason::StartLimit;
        return _pumpOn;
    }
    _set(true, now);
    _reason = Reason::PulseOn;
    _phaseRemaining = _s.onSec;
    return _pumpOn;
}

// ---------------- RollingAvg ----------------

void RollingAvg::add(float v, uint32_t nowSec) {
    _v[_head] = v;
    _ts[_head] = nowSec;
    _head = (_head + 1) % N;
    if (_n < N) _n++;
}

bool RollingAvg::valid(uint32_t nowSec, uint32_t maxAgeSec) const {
    if (_n == 0) return false;
    uint32_t newest = _ts[(_head + N - 1) % N];
    return nowSec - newest <= maxAgeSec;
}

float RollingAvg::value(uint32_t nowSec) const {
    if (_n == 0) return 0;
    float sum = 0;
    uint8_t cnt = 0;
    for (uint8_t i = 0; i < _n; i++) {
        uint8_t idx = (_head + N - 1 - i) % N;
        if (nowSec - _ts[idx] > windowSec) break;
        sum += _v[idx];
        cnt++;
    }
    if (cnt == 0) return _v[(_head + N - 1) % N];    // only stale samples: newest
    return sum / cnt;
}

} // namespace mist
