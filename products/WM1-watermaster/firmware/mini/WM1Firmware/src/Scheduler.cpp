#include "Scheduler.h"
#include <string.h>

void Scheduler::begin() {
  _irrigation.allOff();
  _state = SchedulerState::IDLE;
  _rtPrefs.begin("wm1_rt", false);
}

void Scheduler::_checkpoint(bool active) {
  _rtPrefs.putBool("valid", active);
  if (active) {
    _rtPrefs.putUChar("progId", _activeProgram->id);
    _rtPrefs.putUChar("seqIdx", _activeSeqIndex);
    _rtPrefs.putUInt("elapsed", _elapsedRunSec);
    _lastCheckpointedElapsed = _elapsedRunSec;
  }
}

bool Scheduler::restoreFromNVS(Program* programSlots[], uint8_t programCount) {
  if (!_rtPrefs.getBool("valid", false)) return false;

  uint8_t progId  = _rtPrefs.getUChar("progId", 0);
  uint8_t seqIdx  = _rtPrefs.getUChar("seqIdx", 0);
  uint32_t elapsed = _rtPrefs.getUInt("elapsed", 0);

  for (uint8_t i = 0; i < programCount; i++) {
    if (programSlots[i]->id != progId) continue;
    if (seqIdx >= programSlots[i]->sequenceCount) break;  // stale checkpoint, program shape changed

    _activeProgram = programSlots[i];
    _activeSeqIndex = seqIdx;
    _elapsedRunSec = elapsed;
    _lastCheckpointedElapsed = elapsed;
    _state = SchedulerState::RUNNING;

    Sequence& seq = _activeProgram->sequences[_activeSeqIndex];
    _irrigation.setValveMask(seq.valveMask);  // dosing re-derived from elapsed on the next update() tick
    Serial.printf("[Scheduler] Restored after reboot: '%s' seq %u at elapsed=%us\n",
                  _activeProgram->name, seqIdx, elapsed);
    return true;
  }

  Serial.println("[Scheduler] Runtime checkpoint found but no matching program — ignoring");
  _checkpoint(false);
  return false;
}

void Scheduler::onPowerStateChange(bool powerOk) {
  if (powerOk == _powerOk) return;  // no change
  _powerOk = powerOk;
  _reevaluatePause("power");
}

void Scheduler::onWaterLevelChange(bool ok) {
  if (ok == _waterOk) return;  // no change
  _waterOk = ok;
  _reevaluatePause("water");
}

void Scheduler::pause() {
  if (!_manualOk) return;  // already paused
  _manualOk = false;
  _reevaluatePause("manual");
}

void Scheduler::resume() {
  if (_manualOk) return;  // wasn't manually paused
  _manualOk = true;
  _reevaluatePause("manual");
}

// Shared pause/resume machinery for every independent reason irrigation
// might need to freeze mid-run (IN1 power loss, and now source-dry-run
// protection from L1/L2). Both reasons are tracked as their own flag
// (_powerOk / _waterOk) and combined here rather than each calling
// allOff()/setValveMask() directly — that guards against the case where
// BOTH conditions are bad at once: if power comes back while water is
// still low, this must stay paused, not resume just because the power
// flag alone flipped true.
void Scheduler::_reevaluatePause(const char* reason) {
  bool systemOk = _powerOk && _waterOk && _manualOk;

  if (!systemOk) {
    // Pause immediately, all relays off, hold everything exactly where
    // it is (§3.7's IN1 behavior, now shared by the water-level check
    // too). We deliberately do NOT stop the sequence (_stopSequence)
    // here — that would discard progress. We just freeze it.
    if (_state == SchedulerState::RUNNING) {
      _irrigation.allOff();
      _state = SchedulerState::PAUSED;
      _checkpoint(true);  // capture the latest elapsed right before the condition actually hit
      Serial.printf("[Scheduler] PAUSED (%s) at elapsed=%us\n", reason, _elapsedRunSec);
    }
  } else {
    // Resume exactly where we left off. Because _elapsedRunSec never
    // moved while paused, the sequence still has the same remaining
    // time/volume to go as it did the instant it was interrupted.
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
    if (_state == SchedulerState::RUNNING &&
        _elapsedRunSec - _lastCheckpointedElapsed >= CHECKPOINT_INTERVAL_SEC) {
      _checkpoint(true);
    }
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
    // Bug fix: this was a no-op TODO — done never became true for a
    // VOLUME_BASED sequence, so it would run forever regardless of
    // runTargetLiters, waiting on flow hardware that's now wired up and
    // calibratable (see Sensors.h's flow K-factor). _seqStartVolumeLiters
    // is captured once in _startSequence(), so this covers the
    // sequence's whole span even across an internal pause/resume.
    float litersThisSequence = _sensors.flowTotalLiters() - _seqStartVolumeLiters;
    done = (litersThisSequence >= (float)seq.runTargetLiters);
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

void Scheduler::_startSequence(Program& prog, uint8_t seqIndex, time_t now, bool chainEligible) {
  _activeProgram = &prog;
  _activeSeqIndex = seqIndex;
  _activeChainEligible = chainEligible;
  _elapsedRunSec = 0;
  _seqStartEpoch = now;
  _seqStartVolumeLiters = _sensors.flowTotalLiters();
  Sequence& seq = prog.sequences[seqIndex];

  // Bug fix: this used to set RUNNING and energize relays unconditionally,
  // regardless of _powerOk/_waterOk. That's fine as long as whichever
  // condition went bad does so WHILE something is already running (the
  // normal case _reevaluatePause was built for) — but if it went bad
  // while the Scheduler was IDLE (nothing to pause), the flag flips
  // silently with no visible effect, and _startSequence would then
  // happily start a brand new sequence and turn a valve on into a
  // known-dry source or a known power outage. Starting straight into
  // PAUSED (relays left off) means the existing resume path picks it up
  // exactly like a mid-run pause/resume — nothing extra to write there.
  bool systemOk = _powerOk && _waterOk && _manualOk;
  if (systemOk) {
    _state = SchedulerState::RUNNING;
    _irrigation.setValveMask(seq.valveMask);  // pump auto-follows via IrrigationController
    Serial.printf("[Scheduler] Started '%s' / sequence '%s' (mask=0x%02X)\n",
                  prog.name, seq.name, seq.valveMask);
  } else {
    _state = SchedulerState::PAUSED;
    Serial.printf("[Scheduler] '%s' / sequence '%s' due but system not OK (powerOk=%d waterOk=%d manualOk=%d) — starting paused\n",
                  prog.name, seq.name, _powerOk, _waterOk, _manualOk);
  }
  _checkpoint(true);
}

void Scheduler::_stopSequence(bool completed) {
  _irrigation.allOff();  // pump auto-follows valves to off too
  Serial.printf("[Scheduler] %s '%s' after %us\n",
                completed ? "Completed" : "Stopped",
                _activeProgram ? _activeProgram->name : "?", _elapsedRunSec);

  // Log this sequence's run regardless of whether it completed or was
  // stopped early — "what actually happened" is what the history is
  // for. Volume is a delta against the totalizer reading captured at
  // _startSequence(), so it covers the sequence's whole span even if
  // it was paused partway through.
  if (_activeProgram) {
    Sequence& seq = _activeProgram->sequences[_activeSeqIndex];
    float volumeDelta = _sensors.flowTotalLiters() - _seqStartVolumeLiters;
    String name = String(_activeProgram->name) + " - " + seq.name;
    _history.record(_seqStartEpoch, _elapsedRunSec, name, "auto", volumeDelta);
  }

  // Chain to the next sequence within the same program — a Program is
  // "one or more Sequences" that run one after another, not just the
  // first one (this was a real gap: nothing advanced past sequence 0
  // before this). Only for INTERVAL_DAYS programs, and only on a
  // genuine completion (not a manual/forced stop) — ROTATION mode's
  // whole point is running exactly ONE sequence per trigger, rotating
  // which one across successive days (§3.3c), so it must NOT chain
  // through the rest of the list in a single run.
  if (completed && _activeChainEligible && _activeProgram && _activeProgram->repeatMode == RepeatMode::INTERVAL_DAYS &&
      (uint8_t)(_activeSeqIndex + 1) < _activeProgram->sequenceCount) {
    Program* prog = _activeProgram;
    uint8_t nextSeqIndex = _activeSeqIndex + 1;
    Serial.printf("[Scheduler] Chaining to next sequence (%u/%u) in '%s'\n",
                  nextSeqIndex + 1, prog->sequenceCount, prog->name);
    // Bug fix: this used to pass time(nullptr) — this firmware never
    // calls settimeofday(), so that's an uncalibrated uptime-ish value,
    // not a real epoch (harmless while nothing consumed it; RunHistory
    // now does). _lastTickTime is the RTC-accurate `now` update() was
    // just called with, which is exactly what's needed here since
    // _stopSequence only ever runs from within that same tick.
    _startSequence(*prog, nextSeqIndex, _lastTickTime, /*chainEligible=*/true);
    return;
  }

  _activeProgram = nullptr;
  _state = SchedulerState::IDLE;
  _checkpoint(false);

  // If something was queued behind us, it starts now rather than
  // waiting for its own next-due check — this is the "Sequence 2
  // starts the moment Sequence 1 actually finishes" behavior.
  if (_queuedProgram) {
    Program* next = _queuedProgram;
    uint8_t seqIdx = _queuedSeqIndex;
    _queuedProgram = nullptr;
    // Always schedule-driven — the only thing that ever sets
    // _queuedProgram is _checkDuePrograms finding a due program while
    // busy, never triggerNow() — so this always chains normally.
    _startSequence(*next, seqIdx, _lastTickTime, /*chainEligible=*/true);
  }
}

void Scheduler::addProgram(Program* prog) {
  if (_programCount >= MAX_PROGRAMS) return;
  _programs[_programCount++] = prog;
}

void Scheduler::resetPrograms(Program* slots[], uint8_t count) {
  _programCount = 0;
  for (uint8_t i = 0; i < count && i < MAX_PROGRAMS; i++) {
    _programs[_programCount++] = slots[i];
  }
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

Scheduler::NextRun Scheduler::computeNextRun(time_t now) const {
  NextRun best;
  for (uint8_t i = 0; i < _programCount; i++) {
    Program* prog = _programs[i];
    if (!prog->enabled || !prog->autoStart) continue;

    struct tm baseTm;
    localtime_r(&now, &baseTm);
    baseTm.tm_hour = prog->startHour;
    baseTm.tm_min = prog->startMinute;
    baseTm.tm_sec = 0;
    time_t candidate = mktime(&baseTm);
    if (candidate <= now) candidate += 86400;  // today's slot already passed

    // ROTATION fires every day at its time, and a program that's never
    // run (lastRunStartEpochDay == 0) is due at its very next
    // occurrence — candidate as computed above is already right for
    // both. Only INTERVAL_DAYS with a real prior run needs walking
    // forward to the next day actually satisfying the interval; bounded
    // by intervalDays since the modulo condition is guaranteed to hit
    // within that many days.
    if (prog->repeatMode == RepeatMode::INTERVAL_DAYS && prog->lastRunStartEpochDay != 0) {
      for (uint8_t tries = 0; tries < prog->intervalDays; tries++) {
        long candidateEpochDay = candidate / 86400L;
        long delta = candidateEpochDay - prog->lastRunStartEpochDay;
        long mod = delta % (long)prog->intervalDays;
        if (mod < 0) mod += prog->intervalDays;  // C++'s % can be negative; delta shouldn't be here, but stay defensive
        if (mod == 0) break;
        candidate += 86400;
      }
    }

    if (!best.valid || candidate < best.epoch) {
      best.valid = true;
      best.programId = prog->id;
      best.epoch = candidate;
      strncpy(best.programName, prog->name, sizeof(best.programName) - 1);
      best.programName[sizeof(best.programName) - 1] = '\0';
    }
  }
  return best;
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
      _startSequence(*prog, seqIndex, now, /*chainEligible=*/true);
    } else {
      // Busy — queue it rather than overlap or skip (§3.7 worked example).
      _queuedProgram = prog;
      _queuedSeqIndex = seqIndex;
      _queuedChainEligible = true;
      Serial.printf("[Scheduler] '%s' due but busy — queued\n", prog->name);
    }
  }
}

void Scheduler::triggerNow(Program* prog, uint8_t seqIndex) {
  // Manual "Run this sequence" hook from the app's Programs screen —
  // starts just the ONE requested sequence, bypassing the schedule
  // entirely. chainEligible=false throughout: this must NOT auto-chain
  // into the rest of the program on completion (bug fix — see
  // _startSequence's doc comment for the exact scenario this caused),
  // and if the scheduler is busy when this is called, the queued entry
  // must carry the same false so a real due program queued behind an
  // unrelated manual run doesn't inherit it either. Uses _lastTickTime
  // rather than time(nullptr) for the same reason as the chaining fix
  // above — this runs outside update()'s call chain so it can be up to
  // one loop() iteration (tens of ms) stale, irrelevant for the history
  // record this timestamp ends up in.
  if (_state == SchedulerState::IDLE) {
    _startSequence(*prog, seqIndex, _lastTickTime, /*chainEligible=*/false);
  } else {
    _queuedProgram = prog;
    _queuedSeqIndex = seqIndex;
    _queuedChainEligible = false;
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
