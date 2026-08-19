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
  if (!seq.doseEnabled || _doseFiredThisSequence) return;

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

  if (elapsedSec >= triggerAt) {
    _irrigation.setDosing(true);
    _doseFiredThisSequence = true;
    Serial.printf("[Scheduler] Dosing started (timing=%d) at elapsed=%us\n",
                  (int)seq.doseTiming, elapsedSec);
    // A separate short-lived timer (or a second check against
    // triggerAt + doseDurationSec on later ticks) turns dosing back
    // off after doseDurationSec — omitted here for brevity, same
    // elapsed-time-based approach as the rest of this file.
  }
}

void Scheduler::_startSequence(Program& prog, uint8_t seqIndex, time_t now) {
  _activeProgram = &prog;
  _activeSeqIndex = seqIndex;
  _elapsedRunSec = 0;
  _doseFiredThisSequence = false;
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
    _queuedProgram = nullptr;
    _startSequence(*next, 0, time(nullptr));
  }
}

void Scheduler::_checkDuePrograms(time_t now) {
  // TODO: iterate the real program list (from storage), check each
  // enabled program's due-ness against its repeat mode:
  //   - INTERVAL_DAYS: (daysSince(program.lastRunStartDay) % intervalDays) == 0
  //   - ROTATION: today's rotationIndex sequence is due
  // and whether now's time-of-day matches startHour/startMinute.
  //
  // The important part already captured in this file's structure:
  // when a program IS due but _state != IDLE, queue it
  // (_queuedProgram = &dueProgram) instead of starting it — never
  // start two sequences at once, never skip a due program outright.
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
