#include "Scheduler.h"

void Scheduler::begin() {
  _irrigation.allOff();
  _state = SchedulerState::IDLE;
}

void Scheduler::onPowerStateChange(bool powerOk) {
  if (powerOk == _powerOk) return;  // no change
  _powerOk = powerOk;

  if (!powerOk) {
    // IN1 LOW: pause immediately, all relays off, hold everything
    // exactly where it is (§3.7). We deliberately do NOT stop the
    // sequence (_stopSequence) here — that would discard progress.
    // We just freeze it.
    if (_state == SchedulerState::RUNNING) {
      _irrigation.allOff();
      _state = SchedulerState::PAUSED;
      Serial.printf("[Scheduler] PAUSED (no power) at elapsed=%us\n", _elapsedRunSec);
    }
  } else {
    // IN1 HIGH again: resume exactly where we left off. Because
    // _elapsedRunSec never moved while paused, the sequence still has
    // the same remaining time/volume to go as it did the instant
    // power was lost — the loop() re-applies valveMask/dosing state
    // fresh below on the next update() tick.
    if (_state == SchedulerState::PAUSED && _activeProgram) {
      _irrigation.setValveMask(_activeProgram->sequences[_activeSeqIndex].valveMask);
      // Dosing resumes too, only if it had started and hadn't finished
      // (see _applyDosingForElapsed — it's re-derived from elapsed
      // time each tick, so simply un-pausing is enough; no separate
      // "was dosing active" flag needs restoring here).
      _state = SchedulerState::RUNNING;
      Serial.printf("[Scheduler] RESUMED at elapsed=%us\n", _elapsedRunSec);
    }
  }
}

void Scheduler::update(time_t now) {
  if (_lastTickTime == 0) _lastTickTime = now;
  uint32_t deltaSec = (uint32_t)difftime(now, _lastTickTime);

  // Guard against clock discontinuities — an RTC (re)set, NTP resync,
  // or manual time correction can make `now` jump by minutes/hours in
  // a single tick. Legitimate ticks are ~0-1s apart (loop() runs
  // every 50ms); anything past this threshold is treated as a resync
  // event, not real elapsed run time, so it can't be silently
  // credited to whatever sequence happens to be running. (Bug fix:
  // this previously had no guard at all — see the 35859s "Completed"
  // log after a mid-run RTC resync.)
  static constexpr uint32_t MAX_SANE_DELTA_SEC = 5;
  if (deltaSec > MAX_SANE_DELTA_SEC) {
    Serial.printf("[Scheduler] Clock jumped %us — treating as resync, not elapsed run time\n", deltaSec);
    _lastTickTime = now;
    _checkDuePrograms(now);
    return;
  }

  _lastTickTime = now;

  if (_state == SchedulerState::RUNNING && deltaSec > 0) {
    _elapsedRunSec += deltaSec;
    _tickRunning(now);
  }

  // Whether idle, running, or paused, keep checking for the next due
  // program — a due program while we're busy just gets queued, not
  // dropped (§3.7 worked example: Sequence 2 waits for Sequence 1).
  _checkDuePrograms(now);
}

void Scheduler::_tickRunning(time_t now) {
  Sequence& seq = _activeProgram->sequences[_activeSeqIndex];

  _applyDosingForElapsed(_elapsedRunSec);

  bool done = false;
  if (seq.runMode == RunMode::TIME_BASED) {
    done = (_elapsedRunSec >= seq.runTargetSec);
  } else {
    // TODO: wire to the real flow totalizer once available; today's
    // 1-channel flow input can already drive this for real testing.
    // done = (currentSequenceLiters() >= seq.runTargetLiters);
  }

  if (done) {
    _stopSequence(/*completed=*/true);
  }
}

void Scheduler::_applyDosingForElapsed(uint32_t elapsedSec) {
  Sequence& seq = _activeProgram->sequences[_activeSeqIndex];
  if (!seq.doseEnabled) {
    if (_irrigation.getDosing()) _irrigation.setDosing(false);
    return;
  }

  uint32_t targetSec = (seq.runMode == RunMode::TIME_BASED) ? seq.runTargetSec : 0;
  // For volume-based runs, "mid/end" still need a real elapsed-time
  // estimate — until then this only fully works for time-based runs;
  // fine for now since dosing timing is explicitly a small, short
  // event layered on top, not the primary run-completion mechanism.

  uint32_t triggerAt = 0;
  switch (seq.doseTiming) {
    case DoseTiming::START: triggerAt = 0; break;
    case DoseTiming::MID:   triggerAt = targetSec / 2; break;
    case DoseTiming::END:   triggerAt = (targetSec > seq.doseDurationSec)
                                          ? (targetSec - seq.doseDurationSec) : 0; break;
  }

  // Recomputed from elapsed RUN time on every tick, not a one-shot
  // fire-and-forget — this is what actually makes dosing turn itself
  // off after doseDurationSec, and what makes an IN1 pause/resume
  // mid-dose (§3.3b) "just work": elapsed freezes while paused, so
  // resuming re-evaluates this same window and switches dosing back
  // on if it hadn't finished yet, off if it had.
  bool shouldBeOn = (elapsedSec >= triggerAt) && (elapsedSec < triggerAt + seq.doseDurationSec);
  if (shouldBeOn != _irrigation.getDosing()) {
    _irrigation.setDosing(shouldBeOn);
    Serial.printf("[Scheduler] Dosing %s at elapsed=%us\n",
                  shouldBeOn ? "started" : "stopped", elapsedSec);
  }
}

void Scheduler::_startSequence(Program& prog, uint8_t seqIndex, time_t now) {
  _activeProgram = &prog;
  _activeSeqIndex = seqIndex;
  _elapsedRunSec = 0;
  _state = SchedulerState::RUNNING;

  Sequence& seq = prog.sequences[seqIndex];
  _irrigation.setValveMask(seq.valveMask);  // pump auto-follows via IrrigationController
  Serial.printf("[Scheduler] Started '%s' / sequence '%s' (mask=0x%02X)\n",
                prog.name, seq.name, seq.valveMask);
}

void Scheduler::_stopSequence(bool completed) {
  _irrigation.allOff();  // pump auto-follows valves to off too
  Serial.printf("[Scheduler] %s '%s' after %us\n",
                completed ? "Completed" : "Stopped",
                _activeProgram ? _activeProgram->name : "?", _elapsedRunSec);

  _activeProgram = nullptr;
  _state = SchedulerState::IDLE;

  // If something was queued behind us, it starts now rather than
  // waiting for its own next-due check — this is the "Sequence 2
  // starts the moment Sequence 1 actually finishes" behavior.
  if (_queuedProgram) {
    Program* next = _queuedProgram;
    uint8_t seqIdx = _queuedSeqIndex;
    _queuedProgram = nullptr;
    _startSequence(*next, seqIdx, time(nullptr));
  }
}

void Scheduler::addProgram(Program* prog) {
  if (_programCount >= MAX_PROGRAMS) return;
  _programs[_programCount++] = prog;
}

// Returns the number of whole calendar days between two struct tm
// dates (ignoring time-of-day) — used for both INTERVAL_DAYS
// due-checking and for anchoring a schedule's "day" to its start day
// per §3.3a/§3.3c (an overnight run doesn't get double-counted).
static long daysBetween(const struct tm& a, const struct tm& b) {
  time_t ta = mktime(const_cast<struct tm*>(&a));
  time_t tb = mktime(const_cast<struct tm*>(&b));
  return (long)((tb - ta) / 86400);
}

void Scheduler::_checkDuePrograms(time_t now) {
  struct tm nowTm;
  localtime_r(&now, &nowTm);

  for (uint8_t i = 0; i < _programCount; i++) {
    Program* prog = _programs[i];
    if (!prog->enabled || !prog->autoStart) continue;

    bool timeMatches = (nowTm.tm_hour == prog->startHour && nowTm.tm_min == prog->startMinute);
    if (!timeMatches) continue;

    // Guard against re-triggering every second for the whole matching
    // minute (same class of bug as FG1's Scheduler had) — only fire
    // once per calendar day per program.
    if (prog->lastTriggeredYday == nowTm.tm_yday && prog->lastTriggeredYear == nowTm.tm_year) continue;

    bool due = false;
    if (prog->repeatMode == RepeatMode::INTERVAL_DAYS) {
      if (prog->lastRunStartEpochDay == 0) {
        due = true;  // never run before
      } else {
        long todayEpochDay = now / 86400L;
        long delta = todayEpochDay - prog->lastRunStartEpochDay;
        due = (delta % prog->intervalDays) == 0;
      }
    } else {  // ROTATION
      due = true;  // rotation picks WHICH sequence below, always fires on its own start time
    }

    if (!due) continue;

    prog->lastTriggeredYday = nowTm.tm_yday;
    prog->lastTriggeredYear = nowTm.tm_year;
    prog->lastRunStartEpochDay = now / 86400L;

    uint8_t seqIndex = 0;
    if (prog->repeatMode == RepeatMode::ROTATION) {
      seqIndex = prog->rotationIndex;
      prog->rotationIndex = (prog->rotationIndex + 1) % prog->sequenceCount;
    }

    if (_state == SchedulerState::IDLE) {
      _startSequence(*prog, seqIndex, now);
    } else {
      // Busy — queue it rather than overlap or skip (§3.7 worked example).
      _queuedProgram = prog;
      _queuedSeqIndex = seqIndex;
      Serial.printf("[Scheduler] '%s' due but busy — queued\n", prog->name);
    }
  }
}

void Scheduler::triggerNow(Program* prog, uint8_t seqIndex) {
  // Test/manual hook — starts a program immediately, bypassing its
  // schedule entirely. Not part of the spec; purely for bench testing
  // without waiting on real clock times.
  if (_state == SchedulerState::IDLE) {
    _startSequence(*prog, seqIndex, time(nullptr));
  } else {
    _queuedProgram = prog;
    _queuedSeqIndex = seqIndex;
    Serial.println("[Scheduler] Busy — queued for when current sequence finishes");
  }
}

void Scheduler::manualOverride(uint8_t valveMask, bool pumpDosing) {
  // Manual commands take priority immediately. If something was
  // auto-running, its progress (_elapsedRunSec) is simply left as-is
  // and picked back up — same "hold state, don't discard" pattern as
  // the IN1 pause path, just triggered by the farmer instead of a
  // power event.
  _irrigation.setValveMask(valveMask);
  if (!pumpDosing) _irrigation.setDosing(false);
}

void Scheduler::releaseManualOverride() {
  if (_activeProgram && _state == SchedulerState::RUNNING) {
    Sequence& seq = _activeProgram->sequences[_activeSeqIndex];
    _irrigation.setValveMask(seq.valveMask);
  } else {
    _irrigation.allOff();
  }
}
