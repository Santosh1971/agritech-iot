#pragma once
#include <Arduino.h>
#include <time.h>
#include <Preferences.h>
#include "IrrigationController.h"
#include "Sensors.h"
#include "RunHistory.h"

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
  char name[32];
  Sequence sequences[10];     // up to 10 sequences per program, per spec
  uint8_t sequenceCount;
  RepeatMode repeatMode;
  uint8_t intervalDays;       // INTERVAL_DAYS: 1 = daily, 2 = alternate, ...
  uint8_t rotationIndex;      // ROTATION: which sequence runs "today"
  uint8_t startHour, startMinute;
  bool enabled;
  bool autoStart;

  // Scheduler bookkeeping — in-RAM only for now (see README note on
  // persistence as a follow-up item once this is proven out).
  int16_t lastTriggeredYday = -1;
  int16_t lastTriggeredYear = -1;
  long lastRunStartEpochDay = 0;
};

enum class SchedulerState : uint8_t { IDLE, RUNNING, PAUSED, QUEUED_WAITING };

class Scheduler {
public:
  Scheduler(IrrigationController& irrigation, Sensors& sensors, RunHistory& history)
    : _irrigation(irrigation), _sensors(sensors), _history(history) {}

  void begin();

  // Called once per second (or on every loop tick — cheap either way)
  // with the current wall-clock time from the RTC. DS1307 has no
  // interrupt/alarm registers, so this poll IS the trigger mechanism
  // (§2.1) — there is no interrupt-driven alternative on this RTC.
  void update(time_t now);

  // Wired to IN1 (§3.7). true = power OK (HIGH), false = no power (LOW).
  void onPowerStateChange(bool powerOk);

  // Source dry-run protection from the optional L1/L2 float switches
  // (IN2/IN3) — only ever called when that feature is enabled for this
  // installation (main.cpp gates it; most Minis have no level switches
  // at all and never call this, so _waterOk simply stays true forever).
  // true = water present at L1 or L2, false = both dry, pause like a
  // power loss until level is restored.
  void onWaterLevelChange(bool ok);

  // User-initiated pause/resume — distinct from forceStop(): pause
  // freezes the active sequence exactly like an IN1/water-low pause
  // (elapsed time stops, relays off, resumes from the same point), it
  // just doesn't clear _activeProgram the way a stop does. Feeds the
  // same _reevaluatePause() as power/water, so all three conditions
  // must be OK before anything actually resumes — pausing while water
  // is already low, then hitting Resume, correctly stays paused.
  void pause();
  void resume();

  // Manual override (§6, Manual Control page) — takes priority
  // immediately, and the interrupted automated cycle resumes once
  // released, same machinery as an IN1 pause.
  void manualOverride(uint8_t valveMask, bool pumpDosing);
  void releaseManualOverride();

  // Actually stops whatever's running/paused — clears internal state,
  // not just the physical relays. (Bug fix: previously main.cpp's 'x'
  // handler called irrigation.allOff() directly, which turned relays
  // off but left the Scheduler still believing a sequence was active
  // — it would then reassert valve/dosing state on the next tick as
  // if nothing had happened.)
  //
  // Second bug fix: this used to do NOTHING if _activeProgram was null
  // — which is exactly the case after a manual_set turns a valve on
  // outside of any program. Force Stop would report success while the
  // relay stayed on. allOff() now always runs regardless of whether a
  // program is active.
  void forceStop() {
    if (_activeProgram) {
      _stopSequence(false);
    } else {
      _irrigation.allOff();
    }
    _queuedProgram = nullptr;
    // A stop always fully clears any user pause too — otherwise the
    // NEXT program to start would find _manualOk already false (stuck
    // from a previous run) and refuse to actually energize anything.
    _manualOk = true;
  }

  // Program registration and a bench-testing hook to bypass the
  // schedule and start something immediately.
  void addProgram(Program* prog);
  void triggerNow(Program* prog, uint8_t seqIndex);

  // Bug fix: CommandHandler's set_programs rewrites the shared Program
  // structs' CONTENTS in place, but the Scheduler's own _programs[]
  // list (which _checkDuePrograms actually iterates) was only ever
  // populated once, at boot, from addProgram() calls in main.cpp's
  // setup(). A program defined AFTER boot (the normal case — the app
  // always calls set_programs at runtime) was therefore never
  // registered with the Scheduler at all and could never trigger,
  // regardless of enabled/autoStart/time being correct. Call this
  // instead of relying on boot-time addProgram() calls whenever the
  // program list changes.
  void resetPrograms(Program* slots[], uint8_t count);

  SchedulerState state() const { return _state; }

  // Which condition is holding a PAUSED state — lets the app show
  // "Paused — water low" instead of a bare "Paused", without it having
  // to guess from unrelated fields. Meaningless (empty) unless actually
  // paused; checked in this order since _reevaluatePause pauses the
  // instant ANY one of these goes false, so more than one can be bad
  // at once — order here is just which one the app names first.
  const char* pauseReason() const {
    if (_state != SchedulerState::PAUSED) return "";
    if (!_waterOk) return "water";
    if (!_powerOk) return "power";
    if (!_manualOk) return "manual";
    return "";
  }
  uint32_t elapsedRunSec() const { return _elapsedRunSec; }
  const Program* activeProgram() const { return _activeProgram; }
  uint8_t activeSeqIndex() const { return _activeSeqIndex; }

  // What the Dashboard's "Upcoming" card shows — the single soonest
  // future firing across every enabled+autoStart program, so the app
  // doesn't have to (and can't, accurately: it never sees
  // lastRunStartEpochDay) re-derive interval-day due-ness itself.
  struct NextRun {
    bool valid = false;
    uint8_t programId = 0;
    char programName[32] = "";
    time_t epoch = 0;
  };
  NextRun computeNextRun(time_t now) const;

  // Reboot-survival: call once at boot, AFTER programs are loaded from
  // ProgramStore and registered via addProgram(). If a checkpoint from
  // before the reboot is found and its program id still exists in the
  // freshly-loaded list, the interrupted sequence resumes exactly like
  // an IN1 pause/resume — elapsedRunSec continues from the last
  // checkpoint rather than restarting at 0, so a real power-cycle mid
  // run doesn't throw away progress. Returns true if something resumed.
  bool restoreFromNVS(Program* programSlots[], uint8_t programCount);

private:
  // chainEligible: whether completing THIS sequence should auto-chain
  // into the program's next one. true for anything that started from
  // the actual schedule (_checkDuePrograms, the queued-handoff in
  // _stopSequence, and continuing an already-eligible chain); false
  // for triggerNow()'s ad-hoc "run just this one sequence" manual
  // action. Bug fix: this used to always chain regardless of how the
  // run started — manually running a single mid-program sequence via
  // the app's "Run Sequence" button would then auto-continue into the
  // REST of the program on its own, and if the program's own scheduled
  // start time arrived while that was still running, the real
  // scheduled run got queued behind it — net effect: sequences ran out
  // of order and some ran twice, confirmed by tracing exactly the
  // scenario reported (selecting/running a sequence before a program's
  // scheduled start disturbed that scheduled run).
  void _startSequence(Program& prog, uint8_t seqIndex, time_t now, bool chainEligible = true);
  void _stopSequence(bool completed);
  void _tickRunning(time_t now);
  void _checkDuePrograms(time_t now);
  void _applyDosingForElapsed(uint32_t elapsedSec);
  void _reevaluatePause(const char* reason);

  // Writes (or clears) the runtime checkpoint. Called on every state
  // transition (start/stop/pause) — those are rare/cheap — plus every
  // CHECKPOINT_INTERVAL_SEC of actual run progress, so a real power
  // loss never costs more than that interval's worth of resumed
  // accuracy. Not called every tick: NVS write endurance is finite,
  // and a 3-hour irrigation run ticking every second would be ~10,000
  // writes for no real benefit over a 30s granularity.
  void _checkpoint(bool active);

  static constexpr uint32_t CHECKPOINT_INTERVAL_SEC = 30;
  Preferences _rtPrefs;
  uint32_t _lastCheckpointedElapsed = 0;

  IrrigationController& _irrigation;
  Sensors& _sensors;
  RunHistory& _history;
  SchedulerState _state = SchedulerState::IDLE;

  // What's currently running (or paused mid-run)
  Program* _activeProgram = nullptr;
  uint8_t _activeSeqIndex = 0;
  bool _activeChainEligible = true;  // see _startSequence's chainEligible param

  // Wall-clock start of the CURRENT sequence and the flow totalizer's
  // reading at that instant — captured once in _startSequence(), not
  // reset across an internal pause/resume, so the history record
  // logged in _stopSequence() covers the sequence's whole span
  // (including any paused gap) rather than just its final un-paused
  // stretch.
  time_t _seqStartEpoch = 0;
  float _seqStartVolumeLiters = 0;

  // Elapsed RUN seconds for the active sequence — NOT wall-clock
  // elapsed. This is the field that makes the outage math work: it
  // only increments while un-paused, so a 1-hour outage simply means
  // 1 hour where this counter didn't move, and the sequence still
  // needs to reach its original target from here.
  uint32_t _elapsedRunSec = 0;

  // Anything waiting because a due program found the scheduler busy.
  // Simple single-slot queue is enough given "only one runs at a
  // time" — if this ever needs to hold more than one waiting program,
  // revisit as an actual queue.
  Program* _queuedProgram = nullptr;
  uint8_t _queuedSeqIndex = 0;
  bool _queuedChainEligible = true;  // matches whichever caller queued it — see _startSequence's chainEligible param

  static constexpr uint8_t MAX_PROGRAMS = 10;   // up to 10 programs, per spec
  Program* _programs[MAX_PROGRAMS] = {nullptr};
  uint8_t _programCount = 0;

  bool _powerOk = true;
  bool _waterOk = true;
  bool _manualOk = true;
  time_t _lastTickTime = 0;
};
