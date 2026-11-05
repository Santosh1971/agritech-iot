import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:shared_preferences/shared_preferences.dart';
import '../../common/demo.dart';
import '../models/device_status.dart';
import '../models/history_record.dart';
import '../models/program.dart';
import 'device_service.dart';

/// A pretend WM1-Mini living inside the phone. Follows the firmware's
/// rules (firmware/mini/WM1Firmware/src/Scheduler.cpp,
/// IrrigationController.h):
///  - the pump (RL1) is never switched directly; it runs whenever any
///    valve is open
///  - a Program is a list of Sequences run one after another; a
///    Sequence ends on time or on litres
///  - dosing switches on at start / mid / end of a sequence for its
///    set duration
///  - rotation programs run one sequence per day, in turn
///  - a program that comes due while another is running is queued
///  - power cut / dry tank / pause button freeze the run and it resumes
///    where it left off
class Wm1DemoService implements DeviceService {
  static const _stateKey = 'wm1_demo_state_v1';
  static const _maxHistory = 600;

  final _status = ReplaySubject<DeviceStatus>();
  final _programs = ReplaySubject<List<Program>>();
  final _library = ReplaySubject<List<LibrarySequence>>();
  final _connected = ReplaySubject<bool>();
  final _history = ReplaySubject<List<HistoryRecord>>();
  final _responses = StreamController<Map<String, dynamic>>.broadcast();
  final _debug = StreamController<String>.broadcast();
  final _rand = Random();

  Timer? _tick;
  bool _loaded = false;

  // ---------- saved state ----------
  List<Program> _programList = [];
  List<LibrarySequence> _libraryList = [];
  List<HistoryRecord> _historyList = [];
  RelayNames _names = const RelayNames();
  // Per-program scheduler bookkeeping (the firmware keeps these in RAM).
  final Map<int, int> _lastRunDay = {}; // program id -> day number
  final Map<int, int> _rotationIndex = {};
  String _lastTriggerKey = '';

  // ---------- live outputs ----------
  final List<bool> _valves = [false, false, false, false];
  bool _dosing = false;

  // ---------- running sequence ----------
  SchedulerState _state = SchedulerState.idle;
  String _pauseReason = '';
  Program? _activeProgram;
  int _activeSeq = 0;
  bool _chain = false;
  double _elapsed = 0;
  double _seqLiters = 0;
  DateTime? _seqStart;
  Program? _queuedProgram;
  int _queuedSeq = 0;
  bool _queuedChain = false;

  // Manual runs, recorded to history when switched off.
  final Map<String, DateTime> _manualStart = {};
  final Map<String, double> _manualLiters = {};

  // ---------- sensors ----------
  double _flowTotal = 15234.0;
  double _flowLpm = 0;
  double _p1 = 0, _p2 = 0;
  bool _powerOk = true;
  bool _waterLevelEnabled = false;
  bool _waterOk = true;

  @override
  Stream<DeviceStatus> get statusStream => _status.stream;
  @override
  Stream<List<Program>> get programsStream => _programs.stream;
  @override
  Stream<List<LibrarySequence>> get libraryStream => _library.stream;
  @override
  Stream<bool> get connectedStream => _connected.stream;
  @override
  Stream<bool> get deviceOnlineStream => _connected.stream;
  @override
  Stream<List<HistoryRecord>> get historyStream => _history.stream;

  // Extra members the real LocalService has, kept so the copied
  // screens compile unchanged.
  Stream<Map<String, dynamic>> get responseStream => _responses.stream;
  Stream<String> get debugStream => _debug.stream;
  List<String> get logHistory => const [];
  Future<bool> retryNow() => connect();

  @override
  bool get isConnected => true;

  @override
  Future<bool> connect() async {
    if (!_loaded) {
      await _load();
      _loaded = true;
    }
    _connected.add(true);
    _tick ??= Timer.periodic(const Duration(seconds: 1), (_) => _onTick());
    _programs.add(_copyPrograms());
    _library.add(List.of(_libraryList));
    _history.add(List.of(_historyList));
    _pushStatus();
    return true;
  }

  @override
  void disconnect() {}

  // ---------- persistence ----------
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_stateKey);
    if (raw != null) {
      try {
        final j = jsonDecode(raw) as Map<String, dynamic>;
        _programList = (j['programs'] as List)
            .map((e) => Program.fromJson(e as Map<String, dynamic>))
            .toList();
        _libraryList = (j['library'] as List)
            .map((e) => LibrarySequence.fromJson(e as Map<String, dynamic>))
            .toList();
        _historyList = (j['history'] as List).map((e) {
          final m = e as Map<String, dynamic>;
          return HistoryRecord(
            start: DateTime.fromMillisecondsSinceEpoch(m['t'] as int),
            durationSec: m['d'] as int,
            name: m['n'] as String,
            source: m['s'] as String,
            volumeLiters: (m['v'] as num).toDouble(),
          );
        }).toList();
        _names = RelayNames.fromJson(j['names'] as Map<String, dynamic>?);
        _flowTotal = (j['flowTotal'] as num?)?.toDouble() ?? _flowTotal;
        _waterLevelEnabled = j['waterLevel'] as bool? ?? false;
        _renumber();
        return;
      } catch (_) {}
    }
    _seed();
  }

  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
        _stateKey,
        jsonEncode({
          'programs': _programList.map((p) => p.toJson()).toList(),
          'library': _libraryList.map((l) => l.toJson()).toList(),
          'history': _historyList
              .map((h) => {
                    't': h.start.millisecondsSinceEpoch,
                    'd': h.durationSec,
                    'n': h.name,
                    's': h.source,
                    'v': h.volumeLiters,
                  })
              .toList(),
          'names': {
            'pump': _names.pump,
            'dosing': _names.dosing,
            'valves': _names.valves,
            'pressure1': _names.pressure1,
            'pressure2': _names.pressure2,
            'flow': _names.flow,
            'waterUpper': _names.waterUpper,
            'waterLower': _names.waterLower,
          },
          'flowTotal': _flowTotal,
          'waterLevel': _waterLevelEnabled,
        }));
  }

  void _seed() {
    _names = const RelayNames(
      pump: 'Borewell',
      dosing: 'Fertilizer',
      valves: ['Tomato A', 'Tomato B', 'Mirchi', 'Mango'],
    );
    _programList = [
      Program(name: 'Tomato daily', startHour: 6, startMinute: 0, sequences: [
        Sequence(name: 'Tomato A', valveMask: 0x1, runTargetSec: 20 * 60,
            doseEnabled: true, doseTiming: DoseTiming.mid, doseDurationSec: 5 * 60),
        Sequence(name: 'Tomato B', valveMask: 0x2, runTargetSec: 20 * 60),
      ]),
      Program(name: 'Mirchi + Mango', startHour: 16, startMinute: 30,
          repeatMode: RepeatMode.rotation, sequences: [
        Sequence(name: 'Mirchi 400 L', valveMask: 0x4, runMode: RunMode.volume, runTargetLiters: 400),
        Sequence(name: 'Mango 600 L', valveMask: 0x8, runMode: RunMode.volume, runTargetLiters: 600),
      ]),
      Program(name: 'Weekly flush', enabled: false, intervalDays: 7, startHour: 7, startMinute: 30,
          sequences: [Sequence(name: 'All lines', valveMask: 0xF, runTargetSec: 10 * 60)]),
    ];
    _libraryList = [
      LibrarySequence(sequence: Sequence(name: 'Drip 30 min', valveMask: 0x1, runTargetSec: 30 * 60)),
      LibrarySequence(sequence: Sequence(name: 'Fertigation 15 min', valveMask: 0x3, runTargetSec: 15 * 60,
          doseEnabled: true, doseTiming: DoseTiming.start, doseDurationSec: 5 * 60)),
      LibrarySequence(sequence: Sequence(name: '500 litres', valveMask: 0x4, runMode: RunMode.volume, runTargetLiters: 500)),
    ];
    _renumber();

    // 30 days of believable history.
    _historyList = [];
    final today = DateTime.now();
    for (var d = 30; d >= 1; d--) {
      final day = DateTime(today.year, today.month, today.day).subtract(Duration(days: d));
      final lpm = 13 + _rand.nextDouble() * 2;
      _historyList.add(HistoryRecord(
          start: day.add(const Duration(hours: 6)), durationSec: 1200,
          name: 'Tomato daily - Tomato A', source: 'auto', volumeLiters: _r1(lpm * 20)));
      _historyList.add(HistoryRecord(
          start: day.add(const Duration(hours: 6, minutes: 20)), durationSec: 1200,
          name: 'Tomato daily - Tomato B', source: 'auto', volumeLiters: _r1(lpm * 20)));
      final mirchi = d.isEven;
      final target = mirchi ? 400.0 : 600.0;
      _historyList.add(HistoryRecord(
          start: day.add(const Duration(hours: 16, minutes: 30)),
          durationSec: (target / lpm * 60).round(),
          name: mirchi ? 'Mirchi + Mango - Mirchi 400 L' : 'Mirchi + Mango - Mango 600 L',
          source: 'auto', volumeLiters: target));
      if (d % 5 == 0) {
        _historyList.add(HistoryRecord(
            start: day.add(const Duration(hours: 10, minutes: 5)), durationSec: 900,
            name: 'valve${1 + d % 4}', source: 'manual', volumeLiters: _r1(lpm * 15)));
      }
    }
    _flowTotal = 15234.0;
    _waterLevelEnabled = false;
  }

  static double _r1(double v) => double.parse(v.toStringAsFixed(1));

  // The firmware assigns ids by position whenever the list is saved.
  void _renumber() {
    for (var i = 0; i < _programList.length; i++) {
      _programList[i].id = i + 1;
    }
    _libraryList = [
      for (var i = 0; i < _libraryList.length; i++)
        LibrarySequence(id: i + 1, sequence: _libraryList[i].sequence),
    ];
  }

  List<Program> _copyPrograms() =>
      _programList.map((p) => Program.fromJson({...p.toJson(), 'id': p.id})).toList();

  /// Back to the sample farm with everything off.
  Future<void> reset() async {
    _stopAll(record: false);
    _powerOk = true;
    _waterOk = true;
    _lastRunDay.clear();
    _rotationIndex.clear();
    _seed();
    await _save();
    _programs.add(_copyPrograms());
    _library.add(List.of(_libraryList));
    _history.add(List.of(_historyList));
    _pushStatus();
  }

  // ---------- simulation ----------
  bool get _pumpOn => _valves.any((v) => v);
  int get _openValves => _valves.where((v) => v).length;
  int _dayNumber(DateTime t) =>
      DateTime.utc(t.year, t.month, t.day).millisecondsSinceEpoch ~/ 86400000;

  void _onTick() {
    final now = DateTime.now();
    final speed = DemoSpeed.value.toDouble();
    final t = now.millisecondsSinceEpoch / 1000;

    // Sensors follow the relays.
    if (_pumpOn) {
      _flowLpm = 8.0 + 6.0 * _openValves + sin(t / 6) * 0.5 + _rand.nextDouble() * 0.3;
      _p1 = 2.8 - 0.25 * _openValves + sin(t / 5) * 0.05 + _rand.nextDouble() * 0.03;
      _p2 = _p1 - 0.35 - _rand.nextDouble() * 0.04;
      final liters = _flowLpm / 60 * speed;
      _flowTotal += liters;
      for (final k in _manualStart.keys) {
        if (k != 'dosing') _manualLiters[k] = (_manualLiters[k] ?? 0) + liters;
      }
      if (_state == SchedulerState.running) _seqLiters += liters;
    } else {
      _flowLpm = 0;
      _p1 = max(0, _p1 * 0.5);
      _p2 = max(0, _p2 * 0.5);
    }

    if (_state == SchedulerState.running && _activeProgram != null) {
      _elapsed += speed;
      _tickRunning();
    }
    _checkDue(now);
    _pushStatus();
  }

  void _tickRunning() {
    final seq = _activeProgram!.sequences[_activeSeq];
    _applyDosing(seq);
    final done = seq.runMode == RunMode.time
        ? _elapsed >= seq.runTargetSec
        : _seqLiters >= seq.runTargetLiters;
    if (done) {
      if (seq.runMode == RunMode.volume) _seqLiters = seq.runTargetLiters.toDouble();
      _endSequence(completed: true);
    }
  }

  void _applyDosing(Sequence seq) {
    if (!seq.doseEnabled) {
      _dosing = false;
      return;
    }
    final target = seq.runMode == RunMode.time ? seq.runTargetSec : 0;
    final at = switch (seq.doseTiming) {
      DoseTiming.start => 0,
      DoseTiming.mid => target ~/ 2,
      DoseTiming.end => target > seq.doseDurationSec ? target - seq.doseDurationSec : 0,
    };
    _dosing = _elapsed >= at && _elapsed < at + seq.doseDurationSec;
  }

  void _checkDue(DateTime now) {
    final key = '${now.day}-${now.hour}:${now.minute}';
    if (key == _lastTriggerKey) return;
    _lastTriggerKey = key;
    final today = _dayNumber(now);
    for (final p in _programList) {
      if (!p.enabled || !p.autoStart || p.sequences.isEmpty) continue;
      if (p.startHour != now.hour || p.startMinute != now.minute) continue;
      final last = _lastRunDay[p.id];
      final due = p.repeatMode == RepeatMode.rotation ||
          last == null ||
          (today - last) % max(1, p.intervalDays) == 0;
      if (!due) continue;
      _lastRunDay[p.id] = today;
      var seq = 0;
      if (p.repeatMode == RepeatMode.rotation) {
        seq = (_rotationIndex[p.id] ?? 0) % p.sequences.length;
        _rotationIndex[p.id] = (seq + 1) % p.sequences.length;
      }
      _startOrQueue(p, seq, chain: true);
    }
  }

  void _startOrQueue(Program p, int seq, {required bool chain}) {
    if (_state == SchedulerState.idle) {
      _startSequence(p, seq, chain: chain);
    } else {
      _queuedProgram = p;
      _queuedSeq = seq;
      _queuedChain = chain;
      if (_state == SchedulerState.running || _state == SchedulerState.paused) {
        // state value stays; the dashboard shows the queue via nextRun
      }
    }
  }

  void _startSequence(Program p, int seq, {required bool chain}) {
    _activeProgram = p;
    _activeSeq = seq;
    _chain = chain;
    _elapsed = 0;
    _seqLiters = 0;
    _seqStart = DateTime.now();
    final blocked = !_powerOk || (_waterLevelEnabled && !_waterOk);
    if (blocked) {
      _state = SchedulerState.paused;
      _pauseReason = !_powerOk ? 'power' : 'water';
      _setValveMask(0);
    } else {
      _state = SchedulerState.running;
      _pauseReason = '';
      _setValveMask(p.sequences[seq].valveMask);
      _applyDosing(p.sequences[seq]);
    }
  }

  void _setValveMask(int mask) {
    for (var i = 0; i < 4; i++) {
      _valves[i] = (mask & (1 << i)) != 0;
    }
    if (mask == 0) _dosing = false;
  }

  void _endSequence({required bool completed}) {
    final p = _activeProgram;
    if (p == null) return;
    final seq = p.sequences[_activeSeq];
    _addHistory(HistoryRecord(
      start: _seqStart ?? DateTime.now(),
      durationSec: _elapsed.round(),
      name: '${p.name} - ${seq.name}',
      source: 'auto',
      volumeLiters: _r1(_seqLiters),
    ));
    _activeProgram = null;
    _state = SchedulerState.idle;
    _pauseReason = '';
    _setValveMask(0);
    _dosing = false;

    if (completed && _chain && p.repeatMode == RepeatMode.interval &&
        _activeSeq + 1 < p.sequences.length) {
      _startSequence(p, _activeSeq + 1, chain: true);
    } else if (_queuedProgram != null) {
      final q = _queuedProgram!;
      _queuedProgram = null;
      _startSequence(q, _queuedSeq, chain: _queuedChain);
    }
  }

  void _addHistory(HistoryRecord r) {
    _historyList.add(r);
    if (_historyList.length > _maxHistory) {
      _historyList.removeRange(0, _historyList.length - _maxHistory);
    }
    _history.add(List.of(_historyList));
    _save();
  }

  void _closeManual(String channel) {
    final start = _manualStart.remove(channel);
    final liters = _manualLiters.remove(channel) ?? 0;
    if (start == null) return;
    final secs = DateTime.now().difference(start).inSeconds * DemoSpeed.value;
    _addHistory(HistoryRecord(
        start: start, durationSec: secs, name: channel, source: 'manual',
        volumeLiters: _r1(liters)));
  }

  void _stopAll({bool record = true}) {
    if (_activeProgram != null && record) {
      _endSequence(completed: false);
    }
    _activeProgram = null;
    _queuedProgram = null;
    _state = SchedulerState.idle;
    _pauseReason = '';
    for (final k in _manualStart.keys.toList()) {
      if (record) {
        _closeManual(k);
      } else {
        _manualStart.remove(k);
      }
    }
    _manualLiters.clear();
    _setValveMask(0);
    _dosing = false;
  }

  void _freeze(String reason) {
    if (_state == SchedulerState.running) {
      _state = SchedulerState.paused;
      _pauseReason = reason;
      _setValveMask(0);
    }
  }

  void _unfreeze(String reason) {
    if (_state == SchedulerState.paused && _pauseReason == reason && _activeProgram != null) {
      if (!_powerOk || (_waterLevelEnabled && !_waterOk)) {
        _pauseReason = !_powerOk ? 'power' : 'water';
        return;
      }
      _state = SchedulerState.running;
      _pauseReason = '';
      final seq = _activeProgram!.sequences[_activeSeq];
      _setValveMask(seq.valveMask);
      _applyDosing(seq);
    }
  }

  // ---------- next run (computed device-side in the firmware) ----------
  (Program, DateTime)? _nextRun(DateTime now) {
    (Program, DateTime)? best;
    for (final p in _programList) {
      if (!p.enabled || !p.autoStart || p.sequences.isEmpty) continue;
      for (var add = 0; add < 60; add++) {
        final day = DateTime(now.year, now.month, now.day + add, p.startHour, p.startMinute);
        if (!day.isAfter(now)) continue;
        final last = _lastRunDay[p.id];
        final ok = p.repeatMode == RepeatMode.rotation ||
            last == null ||
            (_dayNumber(day) - last) % max(1, p.intervalDays) == 0;
        if (!ok) continue;
        if (best == null || day.isBefore(best.$2)) best = (p, day);
        break;
      }
    }
    return best;
  }

  void _pushStatus() {
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    final next = _nextRun(now);
    final p = _activeProgram;
    final seq = p?.sequences[_activeSeq];
    final waterL = _waterLevelEnabled ? _waterOk : true;
    _status.add(DeviceStatus(
      deviceId: 'WM1_DEMO',
      state: _state,
      pauseReason: _pauseReason,
      pump: _pumpOn,
      dosing: _dosing,
      valves: List.of(_valves),
      wifiConnected: true,
      wifiRssi: -58 - _rand.nextInt(6),
      forcedLocal: false,
      programsCount: _programList.length,
      rtcDate: '${two(now.day)}/${two(now.month)}/${now.year}',
      rtcTime: '${two(now.hour)}:${two(now.minute)}',
      rtcOk: true,
      relayNames: _names,
      pressure1Bar: double.parse(_p1.toStringAsFixed(2)),
      pressure2Bar: double.parse(_p2.toStringAsFixed(2)),
      flowRateLpm: double.parse(_flowLpm.toStringAsFixed(1)),
      flowTotalLiters: double.parse(_flowTotal.toStringAsFixed(1)),
      flowPulsesPerLiter: 5.5,
      flowTotalPulsesRaw: (_flowTotal * 5.5).round(),
      waterLevelEnabled: _waterLevelEnabled,
      waterL1Ok: waterL,
      waterL2Ok: waterL,
      waterLevelOk: waterL,
      batteryVolts: _powerOk ? 13.4 : 12.1,
      activeProgramId: p?.id,
      activeProgramName: p?.name,
      activeSequenceName: seq?.name,
      activeSeqIndex: p == null ? null : _activeSeq,
      elapsedSec: p == null ? null : _elapsed.round(),
      runTargetSec: seq?.runTargetSec,
      runMode: seq == null ? null : (seq.runMode == RunMode.volume ? 'volume' : 'time'),
      runTargetLiters: seq?.runTargetLiters,
      elapsedLiters: p == null ? null : double.parse(_seqLiters.toStringAsFixed(1)),
      nextRunProgramId: next?.$1.id,
      nextRunProgramName: next?.$1.name,
      nextRunEpoch: next == null
          ? null
          : DateTime.utc(next.$2.year, next.$2.month, next.$2.day, next.$2.hour, next.$2.minute)
                  .millisecondsSinceEpoch ~/
              1000,
    ));
  }

  // ---------- commands ----------
  @override
  void manualSet(String channel, bool state) {
    if (channel == 'dosing') {
      // Dosing needs water moving — same rule as the firmware.
      if (state && !_pumpOn) {
        _pushStatus();
        return;
      }
      _dosing = state;
    } else if (channel.startsWith('valve')) {
      final i = (int.tryParse(channel.substring(5)) ?? 1) - 1;
      if (i < 0 || i > 3) return;
      _valves[i] = state;
      if (!_pumpOn) _dosing = false;
    }
    if (state) {
      _manualStart.putIfAbsent(channel, DateTime.now);
    } else {
      _closeManual(channel);
      if (!_pumpOn && _manualStart.containsKey('dosing')) _closeManual('dosing');
    }
    _pushStatus();
  }

  @override
  void triggerProgram(int programId, int seqIndex) {
    final p = _programList.where((p) => p.id == programId).firstOrNull;
    if (p == null || seqIndex < 0 || seqIndex >= p.sequences.length) return;
    _startOrQueue(p, seqIndex, chain: false);
    _pushStatus();
  }

  @override
  void forceStop() {
    _stopAll();
    _pushStatus();
  }

  @override
  void pause() {
    _freeze('manual');
    _pushStatus();
  }

  @override
  void resume() {
    _unfreeze('manual');
    _pushStatus();
  }

  @override
  void getPrograms() => _programs.add(_copyPrograms());

  @override
  void setPrograms(List<Program> programs) {
    _programList = programs.map((p) => Program.fromJson({...p.toJson(), 'id': p.id})).toList();
    _renumber();
    if (_activeProgram != null &&
        !_programList.any((p) => p.name == _activeProgram!.name)) {
      // Running program was deleted — let the current sequence finish.
    }
    _programs.add(_copyPrograms());
    _save();
    _pushStatus();
  }

  @override
  void getLibrary() => _library.add(List.of(_libraryList));

  @override
  void setLibrary(List<LibrarySequence> library) {
    _libraryList = List.of(library);
    _renumber();
    _library.add(List.of(_libraryList));
    _save();
  }

  @override
  void simulatePowerLoss() {
    _powerOk = false;
    _freeze('power');
    _pushStatus();
  }

  @override
  void simulatePowerRestore() {
    _powerOk = true;
    _unfreeze('power');
    _pushStatus();
  }

  bool get powerOk => _powerOk;
  bool get waterOk => _waterOk;

  @override
  void getHistory({int since = 0, int max = 500}) => _history.add(List.of(_historyList));

  void scanWifi() {}
  void factoryReset() => reset();

  void setRelayNames({
    required String pump,
    required String dosing,
    required List<String> valves,
    required String pressure1,
    required String pressure2,
    required String flow,
    required String waterUpper,
    required String waterLower,
  }) {
    _names = RelayNames(
      pump: pump, dosing: dosing, valves: valves, pressure1: pressure1,
      pressure2: pressure2, flow: flow, waterUpper: waterUpper, waterLower: waterLower,
    );
    _save();
    _pushStatus();
  }

  @override
  void sendRaw(Map<String, dynamic> payload) {
    switch (payload['cmd']) {
      case 'set_water_level_enabled':
        _waterLevelEnabled = payload['enabled'] == true;
        if (!_waterLevelEnabled) {
          _waterOk = true;
          _unfreeze('water');
        }
        _save();
      case 'simulate_water_low':
        _waterOk = false;
        if (_waterLevelEnabled) _freeze('water');
      case 'simulate_water_ok':
        _waterOk = true;
        _unfreeze('water');
      case 'clear_history':
        _historyList.clear();
        _history.add(const []);
        _save();
    }
    _pushStatus();
  }

  @override
  void dispose() {
    _tick?.cancel();
    _status.close();
    _programs.close();
    _library.close();
    _connected.close();
    _history.close();
    _responses.close();
    _debug.close();
  }
}
