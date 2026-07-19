#include "Scheduler.h"

void Scheduler::begin(NVSManager* nvs, RTCManager* rtc,
                      FlowSensor* flow, RelayControl* relay) {
    _nvs   = nvs;
    _rtc   = rtc;
    _flow  = flow;
    _relay = relay;
    _cycleCount = _nvs->loadCycles(_cycles);
    memset(&_state, 0, sizeof(_state));
    Serial.printf("[SCHED] Ready — %d cycles loaded\n", _cycleCount);
}

void Scheduler::reloadCycles() {
    _cycleCount = _nvs->loadCycles(_cycles);
    Serial.printf("[SCHED] Cycles reloaded — %d cycles (run state untouched)\n", _cycleCount);
}

void Scheduler::checkPowerRecovery() {
    RunningState saved;
    if (!_nvs->loadRunningState(saved)) return;
    Serial.println("[SCHED] Power recovery — checking interrupted cycle...");

    if (!_rtc->isTimeSet()) {
        Serial.println("[SCHED] RTC not set — skipping recovery");
        _nvs->clearRunningState();
        return;
    }

    // Manual runs (sentinel id=255) never match a saved cycle — there's no
    // scheduled window to check them against, so they're never resumed.
    // But previously this branch just silently discarded the interrupted
    // run's state entirely, with no history trace of it ever happening —
    // a real gap in "some manual operations are missing from history."
    // Log what was delivered before the power loss, then clear.
    if (saved.cycleId == 255) {
        Serial.println("[SCHED] Interrupted manual run — logging then clearing (not resumed)");
        HistoryEntry h;
        h.timestamp       = _rtc->getUnixTime();
        h.cycleId         = 255;
        h.litersDelivered = saved.litersDelivered;
        h.durationSeconds = (uint16_t)(h.timestamp - saved.startUnix);
        h.cycleName[0]    = '\0';
        h.mode            = LITER_BASED;
        strlcpy(h.status, "manual", sizeof(h.status));
        _nvs->addHistoryEntry(h);
        _nvs->clearRunningState();
        return;
    }

    // Find the cycle
    Cycle* c = nullptr;
    for (uint8_t i = 0; i < _cycleCount; i++) {
        if (_cycles[i].id == saved.cycleId) { c = &_cycles[i]; break; }
    }
    if (!c) { _nvs->clearRunningState(); return; }

    DateTime now   = _rtc->now();
    uint8_t  nowH  = now.hour();
    uint8_t  nowM  = now.minute();
    uint16_t nowMins   = nowH * 60 + nowM;
    uint16_t startMins = c->startHour * 60 + c->startMinute;
    uint16_t endMins   = c->endHour   * 60 + c->endMinute;

    // TIME_BASED has no liter target at all — gate purely on the window.
    // LITER_BASED has no window at all — gate purely on liters remaining.
    // TIME_WINDOW_LITER needs BOTH: still within window AND liters left.
    bool withinWindow = (c->mode == TIME_BASED || c->mode == TIME_WINDOW_LITER)
                        ? (nowMins >= startMins && nowMins < endMins)
                        : true;
    bool hasLiterTarget = (c->mode == LITER_BASED || c->mode == TIME_WINDOW_LITER);
    float remaining = hasLiterTarget ? (c->targetLiters - saved.litersDelivered) : 0;
    bool litersOk = hasLiterTarget ? (remaining > 0) : true;

    if (withinWindow && litersOk) {
        // Resume exactly where it left off — _state = saved carries over
        // the accumulated liters base, so the total keeps climbing from
        // its pre-power-loss value rather than restarting at 0.
        Serial.printf("[SCHED] Resuming cycle %d after power loss — window still open"
                       "%s\n", c->id,
                       hasLiterTarget ? ", liters remaining" : "");
        _state = saved;
        _state.active = true;
        _state.paused = false;
        _flow->resetCount();  // live segment counts fresh from resume; base preserved in _state.litersDelivered
        _relay->on();          // relay + linked LED together (see RelayControl)
    } else {
        // Window already elapsed (or liter target already met) — do NOT
        // switch the pump back on, even if some liters technically
        // remain undelivered. Matches "if cycle time elapsed, don't
        // switch on relay/LED."
        Serial.println("[SCHED] Not resuming — cycle window elapsed or target already met");
        _nvs->clearRunningState();
    }
}

void Scheduler::loop() {
    uint32_t now = millis();

    // Schedule check
    if (!_state.active && now - _lastScheduleCheck >= SCHEDULE_CHECK_INTERVAL_MS) {
        _lastScheduleCheck = now;
        _checkSchedule();
    }

    // Cycle completion check
    if (_state.active && !_state.paused) {
        _checkCycleCompletion();
    }

    // Periodic state save while running — computes the live total
    // (accumulated-before-pause base + current segment) for both the
    // status JSON and NVS persistence, WITHOUT mutating _state.litersDelivered
    // itself. That field is the "base" and must only ever be touched by
    // pauseCycle()'s accumulation — overwriting it here (as earlier code
    // did) silently discarded everything delivered before the most recent
    // pause/resume the next time this fired.
    if (_state.active && now - _lastStateSave >= STATE_SAVE_INTERVAL_MS) {
        _lastStateSave = now;
        RunningState toSave = _state;
        if (!_state.paused) {
            toSave.litersDelivered = _state.litersDelivered + _flow->getLitersDelivered();
        }
        _nvs->saveRunningState(toSave);
    }
}

void Scheduler::_checkSchedule() {
    if (!_rtc->isTimeSet()) return;
    DateTime now  = _rtc->now();
    uint8_t  nowH = now.hour();
    uint8_t  nowM = now.minute();

    for (uint8_t i = 0; i < _cycleCount; i++) {
        Cycle& c = _cycles[i];
        if (!c.enabled) continue;
        if (c.startHour == nowH && c.startMinute == nowM) {
            Serial.printf("[SCHED] Triggering cycle %d at %02d:%02d\n",
                          c.id, nowH, nowM);
            _startCycle(c);
            return;
        }
    }
}

void Scheduler::_startCycle(Cycle& c, bool isRecovery) {
    _flow->resetCount();
    _state.active          = true;
    _state.paused          = false;
    _state.cycleId         = c.id;
    _state.litersDelivered = 0;
    _state.startUnix       = _rtc->getUnixTime();
    strlcpy(_state.startedBy, "auto", sizeof(_state.startedBy));
    _relay->on();
    _nvs->saveRunningState(_state);
    Serial.printf("[SCHED] Cycle %d started (mode=%d, target=%.1fL)\n",
                  c.id, c.mode, c.targetLiters);
}

void Scheduler::_checkCycleCompletion() {
    float delivered = _flow->getLitersDelivered() + _state.litersDelivered;
    bool done = false;

    if (_state.cycleId == 255) {
        // Manual run — has no entry in _cycles[] (sentinel id never
        // matches a real saved cycle), so it's checked independently
        // here using _manualTarget rather than via the `c` lookup below.
        // _manualTarget == 0 means "no target, run until told to stop".
        if (_manualTarget > 0 && delivered >= _manualTarget) done = true;
    } else {
        Cycle* c = nullptr;
        for (uint8_t i = 0; i < _cycleCount; i++) {
            if (_cycles[i].id == _state.cycleId) { c = &_cycles[i]; break; }
        }
        if (c) {
            DateTime now   = _rtc->now();
            uint16_t nowM  = now.hour() * 60 + now.minute();
            uint16_t endM  = c->endHour * 60 + c->endMinute;

            if (c->mode == LITER_BASED && delivered >= c->targetLiters)      done = true;
            if (c->mode == TIME_BASED  && nowM >= endM)                       done = true;
            if (c->mode == TIME_WINDOW_LITER &&
                (delivered >= c->targetLiters || nowM >= endM))               done = true;
        }
    }

    if (done) stopCycle(STOP_COMPLETED);
}

void Scheduler::stopCycle(CycleStopReason reason) {
    if (!_state.active) return;
    _relay->off();
    float delivered = _flow->getLitersDelivered() + _state.litersDelivered;

    HistoryEntry h;
    h.timestamp       = _rtc->getUnixTime();
    h.cycleId         = _state.cycleId;
    h.litersDelivered = delivered;
    h.durationSeconds = (uint16_t)(h.timestamp - _state.startUnix);
    // Safe defaults — manual runs (cycleId==255) never match a saved cycle
    // below, so without this cycleName/mode were left uninitialized
    // (reading garbage stack memory into the stored history entry).
    h.cycleName[0]    = '\0';
    h.mode            = LITER_BASED;

    // Find cycle name
    for (uint8_t i = 0; i < _cycleCount; i++) {
        if (_cycles[i].id == _state.cycleId) {
            strlcpy(h.cycleName, _cycles[i].name, 32);
            h.mode = _cycles[i].mode;
            break;
        }
    }

    const char* statusStr[] = {"completed","stopped","paused","recovered"};
    // Manual runs (cycleId==255) are tagged status="manual" so the app's
    // Manual/Auto filter and "Manual Use" label (which check status=='manual')
    // work correctly — the firmware never produced this value before.
    strlcpy(h.status, (_state.cycleId == 255) ? "manual" : statusStr[(int)reason], 16);

    _nvs->addHistoryEntry(h);
    _nvs->clearRunningState();

    memset(&_state, 0, sizeof(_state));
    _manualTarget = 0;
    _flow->resetCount();

    Serial.printf("[SCHED] Cycle stopped — reason=%d, delivered=%.1fL\n",
                  (int)reason, delivered);
}

void Scheduler::pauseCycle() {
    if (!_state.active || _state.paused) return;
    _relay->off();
    _state.paused          = true;
    _state.litersDelivered += _flow->getLitersDelivered();
    _flow->resetCount();
    _nvs->saveRunningState(_state);
    Serial.println("[SCHED] Cycle paused");
}

void Scheduler::resumeCycle() {
    if (!_state.active || !_state.paused) return;
    _state.paused = false;
    _flow->resetCount();
    _relay->on();
    _nvs->saveRunningState(_state);
    Serial.println("[SCHED] Cycle resumed");
}

void Scheduler::startManual(float liters) {
    if (_state.active) stopCycle(STOP_USER);
    _flow->resetCount();
    _state.active          = true;
    _state.paused          = false;
    _state.cycleId         = 255;  // manual sentinel
    _state.litersDelivered = 0;
    _state.startUnix       = _rtc->getUnixTime();
    strlcpy(_state.startedBy, "manual", sizeof(_state.startedBy));

    // Real fix: this used to be stored in a local `static Cycle
    // manualCycle` that _checkCycleCompletion() never actually looked
    // at (it only searches the real _cycles[] list, which a manual run's
    // sentinel id=255 never matches) — manual liter-target runs could
    // never auto-stop as a result. _manualTarget is read directly by
    // _checkCycleCompletion() below.
    _manualTarget = liters;

    _relay->on();
    _nvs->saveRunningState(_state);
    Serial.printf("[SCHED] Manual start — %.1fL target\n", liters);
}

bool Scheduler::isRunning() { return _state.active && !_state.paused; }
bool Scheduler::isPaused()  { return _state.active && _state.paused; }

RunningState Scheduler::getCurrentState() {
    // Live reporting (status JSON, dashboard) needs the up-to-the-moment
    // total, not just whatever was last periodically saved — same fix as
    // the loop() save above: base (accumulated across past pause/resume
    // segments) + current segment's live flow count, base itself never
    // mutated here.
    RunningState s = _state;
    if (s.active && !s.paused) {
        s.litersDelivered = _state.litersDelivered + _flow->getLitersDelivered();
    }
    return s;
}
