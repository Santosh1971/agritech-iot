#include "Scheduler.h"

void Scheduler::begin(NVSManager* nvs, RTCManager* rtc,
                      FlowSensor* flow, RelayControl* relay) {
    _nvs   = nvs;
    _rtc   = rtc;
    _flow  = flow;
    _relay = relay;
    _cycleCount = _nvs->loadCycles(_cycles);
    memset(&_state, 0, sizeof(_state));
    _lastScheduleCheck = 0;
    // Deliberately does NOT call _checkSchedule() here anymore. main.cpp's
    // setup() calls checkPowerRecovery() right after this returns, and
    // THAT must get first refusal on a mid-run cycle interrupted by a
    // power loss -- resuming it with its accumulated progress intact.
    // The very first loop()/update() tick (within ~1s, guaranteed since
    // _lastScheduleCheck=0 and setup() itself takes well over a second
    // for WiFi/NTP/MQTT) naturally runs _checkSchedule() immediately
    // after, so nothing is lost by removing the explicit call here --
    // it's just now correctly ordered AFTER checkPowerRecovery() instead
    // of racing ahead of it. Confirmed on real hardware: with the old
    // ordering, a missed-cycle catch-up firing here would set
    // _state.active=true before checkPowerRecovery() got a chance to
    // run, causing it to skip a genuine mid-run resume entirely and
    // restart the cycle from zero instead of continuing it.
    Serial.printf("[SCHED] Ready — %d cycles loaded\n", _cycleCount);
}

void Scheduler::reloadCycles() {
    _cycleCount = _nvs->loadCycles(_cycles);
    Serial.printf("[SCHED] Cycles reloaded — %d cycles (run state untouched)\n", _cycleCount);
}

void Scheduler::checkPowerRecovery() {
    // begin() (called just before this, in main.cpp's setup()) already
    // runs an initial _checkSchedule() -- which can now start a missed
    // cycle fresh via the catch-up logic above. If that happened,
    // _state.active is already correctly set and there is nothing to
    // "recover": blindly continuing below would overwrite that fresh
    // state with whatever (possibly stale) RunningState happens to be
    // sitting in NVS from an earlier, unrelated interrupted run --
    // confirmed happening on real hardware: a missed-cycle catch-up
    // was immediately clobbered by a leftover saved state from earlier
    // testing, resetting flow count and re-"resuming" instead of
    // leaving the fresh start alone.
    if (_state.active) {
        Serial.println("[SCHED] Skipping power recovery — a cycle is already active (likely just caught up on boot)");
        return;
    }

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

    // TIME_BASED has no liter target at all — gate purely on remaining
    // duration. LITER_BASED has no duration at all — gate purely on
    // liters remaining. TIME_WINDOW_LITER needs BOTH still unmet: some
    // duration remaining AND some liters remaining.
    bool hasDuration = (c->mode == TIME_BASED || c->mode == TIME_WINDOW_LITER);
    uint32_t durationTargetSecs = (uint32_t)c->durationMinutes * 60;
    bool durationOk = hasDuration ? (saved.elapsedSeconds < durationTargetSecs) : true;

    bool hasLiterTarget = (c->mode == LITER_BASED || c->mode == TIME_WINDOW_LITER);
    float remaining = hasLiterTarget ? (c->targetLiters - saved.litersDelivered) : 0;
    bool litersOk = hasLiterTarget ? (remaining > 0) : true;

    if (durationOk && litersOk) {
        // Resume exactly where it left off — _state = saved carries over
        // both the accumulated liters base and the accumulated active-
        // duration base, so both totals keep climbing from their
        // pre-power-loss values rather than restarting. segmentStartUnix
        // is reset to now so the outage itself is never counted as
        // active time — only time the pump is genuinely running counts
        // toward the duration target, the same way outage time already
        // never counted toward a liter target (no flow = no liters).
        Serial.printf("[SCHED] Resuming cycle %d after power loss%s%s\n", c->id,
                       hasDuration    ? " — duration remaining" : "",
                       hasLiterTarget ? " — liters remaining"   : "");
        _state = saved;
        _state.active = true;
        _state.paused = false;
        _state.segmentStartUnix = _rtc->getUnixTime();
        _flow->resetCount();  // live segment counts fresh from resume; base preserved in _state.litersDelivered
        _relay->on();          // relay + linked LED together (see RelayControl)
    } else {
        // Duration already fully elapsed and/or liter target already met
        // before the outage — do NOT switch the pump back on. Matches
        // "if cycle target already met, don't switch on relay/LED."
        Serial.println("[SCHED] Not resuming — duration/target already met");
        _nvs->clearRunningState();
    }
}

void Scheduler::checkScheduleNow() {
    // Only meaningful the moment right after checkPowerRecovery() on
    // boot -- update()'s own !_state.active gate (and _checkSchedule()'s
    // own defensive guard) means this is a safe no-op if a cycle is
    // already active (e.g. checkPowerRecovery() just resumed one).
    _lastScheduleCheck = millis();
    _checkSchedule();
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
            toSave.elapsedSeconds  = _state.elapsedSeconds +
                                      (uint32_t)(_rtc->getUnixTime() - _state.segmentStartUnix);
        }
        _nvs->saveRunningState(toSave);
    }
}

void Scheduler::_checkSchedule() {
    // Defensive — update() only calls this while !_state.active, and
    // begin() no longer calls it directly at all (see begin()'s comment),
    // but guarding here too means this is correct regardless of how or
    // when it's called, not dependent on callers remembering the rule.
    if (_state.active) return;
    if (!_rtc->isTimeSet()) return;
    DateTime now  = _rtc->now();
    uint8_t  nowH = now.hour();
    uint8_t  nowM = now.minute();

    for (uint8_t i = 0; i < _cycleCount; i++) {
        Cycle& c = _cycles[i];
        if (!c.enabled) continue;
        if (c.startHour == nowH && c.startMinute == nowM) {
            // Don't re-fire the same cycle again within the same start
            // minute (see header comment) -- a cycle that completes
            // near-instantly (0-minute duration, misconfigured or from
            // a stale app build not yet sending duration) would
            // otherwise retrigger on every ~1s tick for the rest of the
            // minute, chattering the relay continuously.
            if (c.id == _lastTrigCycleId && nowH == _lastTrigHour && nowM == _lastTrigMinute) {
                continue;
            }
            Serial.printf("[SCHED] Triggering cycle %d at %02d:%02d\n",
                          c.id, nowH, nowM);
            _lastTrigCycleId = c.id;
            _lastTrigHour    = nowH;
            _lastTrigMinute  = nowM;
            _startCycle(c);
            return;
        }
    }

    // No cycle matched the exact current minute -- check whether any
    // enabled cycle's scheduled start already passed today and it
    // hasn't run yet (e.g. the device was down across a power outage
    // spanning its start time, so the exact-minute match above never
    // got the chance to fire for it). If so, start it now, for its
    // full configured duration/target, exactly as if it were starting
    // on time -- confirmed as the expected behavior, not a partial or
    // shortened make-up run. Only reached when nothing is currently
    // active (update() only calls _checkSchedule() while !_state.active).
    _checkMissedCycles(now, nowH, nowM);
}

void Scheduler::_checkMissedCycles(const DateTime& now, uint8_t nowH, uint8_t nowM) {
    for (uint8_t i = 0; i < _cycleCount; i++) {
        Cycle& c = _cycles[i];
        if (!c.enabled) continue;

        // Cycles run "everyday" with no explicit date field -- compare
        // purely by hour:minute against right now.
        bool startTimePassed = (c.startHour < nowH) ||
                                (c.startHour == nowH && c.startMinute < nowM);
        if (!startTimePassed) continue;

        if (_hasRunSinceScheduledStart(c, now)) continue;

        Serial.printf("[SCHED] Missed cycle %d (scheduled %02d:%02d) — catching up now at %02d:%02d\n",
                      c.id, c.startHour, c.startMinute, nowH, nowM);
        _startCycle(c);
        // One catch-up per check, same as the exact-match loop above --
        // if more than one cycle was missed (a long outage spanning
        // several start times), the next tick picks up the next one
        // once this one finishes, naturally serializing them.
        return;
    }
}

bool Scheduler::_hasRunSinceScheduledStart(const Cycle& c, const DateTime& now) {
    // Deliberately anchored on the cycle's CURRENT configured start
    // time today, not just "any history entry for this cycle ID
    // today" -- confirmed as a real bug on hardware: a cycle
    // rescheduled to a new time later the same day (e.g. reconfigured
    // from 17:55 to 18:10 mid-session) was incorrectly treated as
    // "already handled today" because of its earlier run under the
    // OLD schedule, so the catch-up never fired for the new time.
    // Anchoring on the scheduled time itself means a run recorded
    // before that time doesn't count, but one at or after it does --
    // correctly covering both a normal single run and a same-day
    // reschedule.
    DateTime scheduledToday(now.year(), now.month(), now.day(), c.startHour, c.startMinute, 0);
    uint32_t scheduledStart = scheduledToday.unixtime();
    uint32_t nowTs          = now.unixtime();

    HistoryEntry entries[HISTORY_MAX_ENTRIES];
    uint8_t count = _nvs->getHistoryInRange(entries, HISTORY_MAX_ENTRIES, scheduledStart, nowTs);
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].cycleId == c.id) return true;
    }
    return false;
}

void Scheduler::_startCycle(Cycle& c, bool isRecovery) {
    _flow->resetCount();
    _state.active          = true;
    _state.paused          = false;
    _state.cycleId         = c.id;
    _state.litersDelivered = 0;
    _state.startUnix       = _rtc->getUnixTime();
    _state.elapsedSeconds    = 0;
    _state.segmentStartUnix  = _state.startUnix;
    strlcpy(_state.startedBy, "auto", sizeof(_state.startedBy));
    _relay->on();
    _nvs->saveRunningState(_state);
    Serial.printf("[SCHED] Cycle %d started (mode=%d, duration=%dmin, target=%.1fL)\n",
                  c.id, c.mode, c.durationMinutes, c.targetLiters);
}

void Scheduler::_checkCycleCompletion() {
    float delivered = _flow->getLitersDelivered() + _state.litersDelivered;
    // Live elapsed active-duration = persisted base + time since the
    // current active segment began. Only outage/paused time is excluded
    // (this function is only called while active && !paused, so the
    // "since segment began" delta here is always genuine running time).
    uint32_t elapsed = _state.elapsedSeconds +
                        (uint32_t)(_rtc->getUnixTime() - _state.segmentStartUnix);
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
            uint32_t durationTargetSecs = (uint32_t)c->durationMinutes * 60;

            if (c->mode == LITER_BASED && delivered >= c->targetLiters)         done = true;
            if (c->mode == TIME_BASED  && elapsed >= durationTargetSecs)         done = true;
            if (c->mode == TIME_WINDOW_LITER &&
                (delivered >= c->targetLiters || elapsed >= durationTargetSecs)) done = true;
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
    _state.elapsedSeconds  += (uint32_t)(_rtc->getUnixTime() - _state.segmentStartUnix);
    _flow->resetCount();
    _nvs->saveRunningState(_state);
    Serial.println("[SCHED] Cycle paused");
}

void Scheduler::resumeCycle() {
    if (!_state.active || !_state.paused) return;
    _state.paused = false;
    _state.segmentStartUnix = _rtc->getUnixTime();
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
    _state.elapsedSeconds    = 0;
    _state.segmentStartUnix  = _state.startUnix;
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
        s.elapsedSeconds  = _state.elapsedSeconds +
                             (uint32_t)(_rtc->getUnixTime() - _state.segmentStartUnix);
    }
    return s;
}
