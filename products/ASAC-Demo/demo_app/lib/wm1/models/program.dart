enum DoseTiming { start, mid, end }
enum RunMode { time, volume }
enum RepeatMode { interval, rotation }

DoseTiming doseTimingFromStr(String s) => switch (s) {
      'start' => DoseTiming.start,
      'end' => DoseTiming.end,
      _ => DoseTiming.mid,
    };
String doseTimingToStr(DoseTiming t) => switch (t) {
      DoseTiming.start => 'start',
      DoseTiming.mid => 'mid',
      DoseTiming.end => 'end',
    };

/// One step within a Program: which of the 4 valves open together, for
/// how long (or how many liters), and an optional single dosing event.
/// Mirrors firmware/mini/WM1Firmware/src/Scheduler.h's Sequence struct —
/// RL1 (pump) is never selected here, it auto-follows whenever any valve
/// in valveMask is on.
class Sequence {
  String name;
  int valveMask; // bit 0..3 = valve 1..4 (RL3-RL6)
  bool doseEnabled;
  DoseTiming doseTiming;
  int doseDurationSec;
  RunMode runMode;
  int runTargetSec;
  int runTargetLiters;

  Sequence({
    this.name = 'Sequence',
    this.valveMask = 0,
    this.doseEnabled = false,
    this.doseTiming = DoseTiming.mid,
    this.doseDurationSec = 600,
    this.runMode = RunMode.time,
    this.runTargetSec = 1800,
    this.runTargetLiters = 0,
  });

  bool valveOn(int i) => (valveMask & (1 << i)) != 0;
  void setValve(int i, bool on) {
    valveMask = on ? (valveMask | (1 << i)) : (valveMask & ~(1 << i));
  }

  Sequence copy() => Sequence(
        name: name,
        valveMask: valveMask,
        doseEnabled: doseEnabled,
        doseTiming: doseTiming,
        doseDurationSec: doseDurationSec,
        runMode: runMode,
        runTargetSec: runTargetSec,
        runTargetLiters: runTargetLiters,
      );

  factory Sequence.fromJson(Map<String, dynamic> j) => Sequence(
        name: j['name'] as String? ?? 'Sequence',
        valveMask: (j['valveMask'] as num?)?.toInt() ?? 0,
        doseEnabled: j['doseEnabled'] as bool? ?? false,
        doseTiming: doseTimingFromStr(j['doseTiming'] as String? ?? 'mid'),
        doseDurationSec: (j['doseDurationSec'] as num?)?.toInt() ?? 600,
        runMode: (j['runMode'] == 'volume') ? RunMode.volume : RunMode.time,
        runTargetSec: (j['runTargetSec'] as num?)?.toInt() ?? 1800,
        runTargetLiters: (j['runTargetLiters'] as num?)?.toInt() ?? 0,
      );

  Map<String, dynamic> toJson() => {
        'name': name,
        'valveMask': valveMask,
        'doseEnabled': doseEnabled,
        'doseTiming': doseTimingToStr(doseTiming),
        'doseDurationSec': doseDurationSec,
        'runMode': runMode == RunMode.volume ? 'volume' : 'time',
        'runTargetSec': runTargetSec,
        'runTargetLiters': runTargetLiters,
      };
}

/// A reusable Sequence template saved to the device's shared library —
/// COPY semantics: picking one to add to a Program copies its data in;
/// editing/deleting the library entry afterward never changes a
/// program that already copied it (see firmware's SequenceLibrary.h).
class LibrarySequence {
  final int id; // assigned positionally by firmware on save — see CommandHandler::_handleSetLibrary
  final Sequence sequence;

  LibrarySequence({this.id = 0, required this.sequence});

  factory LibrarySequence.fromJson(Map<String, dynamic> j) =>
      LibrarySequence(id: (j['id'] as num?)?.toInt() ?? 0, sequence: Sequence.fromJson(j));

  Map<String, dynamic> toJson() => sequence.toJson();
}

/// A named schedule: an ordered list of Sequences (each its own valve
/// combination + duration + optional dosing), run one after another
/// whenever the Program is due — see repeatMode. Mirrors firmware's
/// Program struct exactly, minus the in-RAM-only scheduler bookkeeping
/// fields (lastTriggeredYday etc.) which never leave the device.
class Program {
  int id; // assigned positionally by the firmware on save — see CommandHandler::_handleSetPrograms
  String name;
  bool enabled;
  bool autoStart;
  RepeatMode repeatMode;
  int intervalDays;
  int startHour;
  int startMinute;
  List<Sequence> sequences;

  Program({
    this.id = 0,
    this.name = 'Program',
    this.enabled = true,
    this.autoStart = true,
    this.repeatMode = RepeatMode.interval,
    this.intervalDays = 1,
    this.startHour = 6,
    this.startMinute = 0,
    List<Sequence>? sequences,
  }) : sequences = sequences ?? [Sequence()];

  factory Program.fromJson(Map<String, dynamic> j) => Program(
        id: (j['id'] as num?)?.toInt() ?? 0,
        name: j['name'] as String? ?? 'Program',
        enabled: j['enabled'] as bool? ?? true,
        autoStart: j['autoStart'] as bool? ?? true,
        repeatMode:
            (j['repeatMode'] == 'rotation') ? RepeatMode.rotation : RepeatMode.interval,
        intervalDays: (j['intervalDays'] as num?)?.toInt() ?? 1,
        startHour: (j['startHour'] as num?)?.toInt() ?? 6,
        startMinute: (j['startMinute'] as num?)?.toInt() ?? 0,
        sequences: (j['sequences'] as List? ?? [])
            .map((e) => Sequence.fromJson(e as Map<String, dynamic>))
            .toList(),
      );

  Map<String, dynamic> toJson() => {
        'name': name,
        'enabled': enabled,
        'autoStart': autoStart,
        'repeatMode': repeatMode == RepeatMode.rotation ? 'rotation' : 'interval',
        'intervalDays': intervalDays,
        'startHour': startHour,
        'startMinute': startMinute,
        'sequences': sequences.map((s) => s.toJson()).toList(),
      };

  String get startTimeStr {
    final h = startHour % 24;
    final period = h >= 12 ? 'PM' : 'AM';
    final h12 = h % 12 == 0 ? 12 : h % 12;
    return '$h12:${startMinute.toString().padLeft(2, '0')} $period';
  }

  String get repeatLabel => repeatMode == RepeatMode.rotation
      ? 'Rotation (${sequences.length} sequences)'
      : (intervalDays == 1 ? 'Daily' : 'Every $intervalDays days');
}
