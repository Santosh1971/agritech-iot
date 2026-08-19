#pragma once
#include <Arduino.h>
#include <time.h>
#include "IrrigationController.h"

// Core scheduling engine. This is where the trickiest rules from the
// spec live:
//   - elapsed RUN time (not clock time) is what's preserved across a
//     pause, so an outage pushes the sequence's finish time back by
//     however long the outage lasted (§3.7 worked example)
//   - only one sequence runs at a time — if the next program's start
//     time arrives while the current one is still running (because it
//     got delayed), it queues instead of overlapping or being skipped
//   - a schedule's "day" is anchored to its START day, so a 10PM
//     schedule that finishes at 2AM the next day is still "yesterday's"
//     for interval/rotation purposes (§3.3a/§3.3c)
//   - dosing fires once, at a configurable point (start/mid/end)
//     relative to the SEQUENCE's own run time, and is itself subject
//     to the same pause/resume as everything else (§3.3b)

enum class DoseTiming : uint8_t { START, MID, END };
enum class RunMode : uint8_t { TIME_BASED, VOLUME_BASED };
enum class RepeatMode : uint8_t { INTERVAL_DAYS, ROTATION };

struct Sequence {
  uint8_t id;
  char name[24];
  uint8_t valveMask;          // bit N = valve N (0..3, RL3-RL6)
  bool doseEnabled;
  DoseTiming doseTiming;
  uint16_t doseDurationSec;   // typically short, e.g. ~600s (10 min)
  RunMode runMode;
  uint32_t runTargetSec;      // used when runMode == TIME_BASED
  uint32_t runTargetLiters;   // used when runMode == VOLUME_BASED
};

struct Program {
  uint8_t id;
  char name[24];
  Sequence sequences[6];      // small fixed cap; grow if needed
  uint8_t sequenceCount;
  RepeatMode repeatMode;
  uint8_t intervalDays;       // INTERVAL_DAYS: 1 = daily, 2 = alternate, ...
  uint8_t rotationIndex;      // ROTATION: which sequence runs "today"
  uint8_t startHour, startMinute;
  bool enabled;
  bool autoStart;
};

enum class SchedulerState : uint8_t { IDLE, RUNNING, PAUSED, QUEUED_WAITING };

class Scheduler {
public:
  Scheduler(IrrigationController& irrigation) : _irrigation(irrigation) {}

  void begin();

  // Called once per second (or on every loop tick — cheap either way)
  // with the current wall-clock time from the RTC. DS1307 has no
  // interrupt/alarm registers, so this poll IS the trigger mechanism
  // (§2.1) — there is no interrupt-driven alternative on this RTC.
  void update(time_t now);

  // Wired to IN1 (§3.7). true = power OK (HIGH), false = no power (LOW).
  void onPowerStateChange(bool powerOk);

  // Manual override (§6, Manual Control page) — takes priority
  // immediately, and the interrupted automated cycle resumes once
  // released, same machinery as an IN1 pause.
  void manualOverride(uint8_t valveMask, bool pumpDosing);
  void releaseManualOverride();

  SchedulerState state() const { return _state; }

private:
  void _startSequence(Program& prog, uint8_t seqIndex, time_t now);
  void _stopSequence(bool completed);
  void _tickRunning(time_t now);
  void _checkDuePrograms(time_t now);
  void _applyDosingForElapsed(uint32_t elapsedSec);

  IrrigationController& _irrigation;
  SchedulerState _state = SchedulerState::IDLE;

  // What's currently running (or paused mid-run)
  Program* _activeProgram = nullptr;
  uint8_t _activeSeqIndex = 0;

  // Elapsed RUN seconds for the active sequence — NOT wall-clock
  // elapsed. This is the field that makes the outage math work: it
  // only increments while un-paused, so a 1-hour outage simply means
  // 1 hour where this counter didn't move, and the sequence still
  // needs to reach its original target from here.
  uint32_t _elapsedRunSec = 0;
  bool _doseFiredThisSequence = false;

  // Anything waiting because a due program found the scheduler busy.
  // Simple single-slot queue is enough given "only one runs at a
  // time" — if this ever needs to hold more than one waiting program,
  // revisit as an actual queue.
  Program* _queuedProgram = nullptr;

  bool _powerOk = true;
  time_t _lastTickTime = 0;
};
