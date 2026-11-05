import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:shared_preferences/shared_preferences.dart';
import '../../common/demo.dart';
import '../models/cycle.dart';
import '../models/device_status.dart';
import '../models/history_entry.dart';
import 'device_service.dart';

/// A pretend FG1 FlowGuard living inside the phone. Follows the same
/// rules as the firmware's Scheduler (lib/Scheduler/Scheduler.cpp):
/// cycles fire at their start minute every day, finish on litres, time,
/// or whichever comes first, a manual run is cycle id 255 with an
/// optional litre target, and every finished run is written to history.
class Fg1DemoService implements DeviceService {
  static const _cyclesKey = 'fg1_demo_cycles';
  static const _historyKey = 'fg1_demo_history';
  static const _manualCycleId = 255;
  static const _maxHistory = 400;

  final _status = ReplaySubject<DeviceStatus>();
  final _cycles = ReplaySubject<List<Cycle>>();
  final _history = ReplaySubject<List<HistoryEntry>>();
  final _connected = ReplaySubject<bool>();
  final _rand = Random();

  Timer? _tick;
  bool _loaded = false;

  List<Cycle> _cycleList = [];
  List<HistoryEntry> _historyList = [];

  // Running state — mirrors Scheduler's RunningState.
  bool _active = false;
  bool _paused = false;
  int _cycleId = 0;
  double _liters = 0;
  double _elapsed = 0; // active (pump-on) seconds, fractional for speed-up
  int _startUnix = 0;
  String _startedBy = '';
  double _manualTarget = 0;
  // Remembers which cycle already fired in the current minute so it
  // doesn't re-trigger every tick (same guard as the firmware).
  String _lastTrigger = '';

  double _flowLpm = 0; // drifts a little so the numbers look alive

  @override
  Stream<DeviceStatus> get statusStream => _status.stream;
  @override
  Stream<List<HistoryEntry>> get historyStream => _history.stream;
  @override
  Stream<List<Cycle>> get cyclesStream => _cycles.stream;
  @override
  Stream<bool> get connectedStream => _connected.stream;
  @override
  Stream<bool> get deviceOnlineStream => _connected.stream;
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
    _cycles.add(List.of(_cycleList));
    _history.add(List.of(_historyList));
    _pushStatus();
    return true;
  }

  // ---------- device-clock helpers ----------
  // The firmware stores IST wall-clock fields as if they were UTC; the
  // app's models undo that. Do the same so the screens read correctly.
  static int _epoch(DateTime wall) => DateTime.utc(
          wall.year, wall.month, wall.day, wall.hour, wall.minute, wall.second)
      .millisecondsSinceEpoch ~/
      1000;
  int get _nowEpoch => _epoch(DateTime.now());

  // ---------- persistence ----------
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    try {
      final c = prefs.getString(_cyclesKey);
      final h = prefs.getString(_historyKey);
      if (c != null && h != null) {
        _cycleList = (jsonDecode(c) as List)
            .map((e) => Cycle.fromJson(e as Map<String, dynamic>))
            .toList();
        _historyList = (jsonDecode(h) as List)
            .map((e) => HistoryEntry.fromJson(e as Map<String, dynamic>))
            .toList();
        return;
      }
    } catch (_) {}
    _seed();
  }

  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_cyclesKey, jsonEncode(_cycleList.map((c) => c.toJson()).toList()));
    await prefs.setString(
        _historyKey, jsonEncode(_historyList.map((h) => h.toJson()).toList()));
  }

  void _seed() {
    _cycleList = [
      Cycle(id: 1, name: 'Morning', startHour: 6, startMinute: 0, durationMinutes: 45,
          mode: OperationMode.literBased, targetLiters: 800, enabled: true),
      Cycle(id: 2, name: 'Evening', startHour: 17, startMinute: 30, durationMinutes: 30,
          mode: OperationMode.timeBased, targetLiters: 0, enabled: true),
      Cycle(id: 3, name: 'Fertigation', startHour: 11, startMinute: 0, durationMinutes: 20,
          mode: OperationMode.timeWindowLiter, targetLiters: 300, enabled: false),
    ];
    // 30 days of believable history.
    _historyList = [];
    final today = DateTime.now();
    for (var d = 30; d >= 1; d--) {
      final day = DateTime(today.year, today.month, today.day).subtract(Duration(days: d));
      final lpm = 17 + _rand.nextDouble() * 3;
      final mLit = 780 + _rand.nextDouble() * 40;
      _historyList.add(HistoryEntry(
        timestamp: _epoch(day.add(const Duration(hours: 6))),
        cycleId: 1, cycleName: 'Morning', mode: OperationMode.literBased,
        litersDelivered: double.parse(mLit.toStringAsFixed(1)),
        durationSeconds: (mLit / lpm * 60).round(),
        status: 'completed',
      ));
      final eLit = lpm * 30;
      _historyList.add(HistoryEntry(
        timestamp: _epoch(day.add(const Duration(hours: 17, minutes: 30))),
        cycleId: 2, cycleName: 'Evening', mode: OperationMode.timeBased,
        litersDelivered: double.parse(eLit.toStringAsFixed(1)),
        durationSeconds: 30 * 60,
        status: d % 9 == 0 ? 'stopped' : 'completed',
      ));
      if (d % 4 == 0) {
        final man = 60 + _rand.nextDouble() * 120;
        _historyList.add(HistoryEntry(
          timestamp: _epoch(day.add(const Duration(hours: 9, minutes: 15))),
          cycleId: _manualCycleId, cycleName: 'Manual', mode: OperationMode.literBased,
          litersDelivered: double.parse(man.toStringAsFixed(1)),
          durationSeconds: (man / lpm * 60).round(),
          status: 'manual',
        ));
      }
    }
  }

  /// Back to the sample setup with everything off.
  Future<void> reset() async {
    _active = false;
    _paused = false;
    _liters = 0;
    _elapsed = 0;
    _seed();
    await _save();
    _cycles.add(List.of(_cycleList));
    _history.add(List.of(_historyList));
    _pushStatus();
  }

  // ---------- simulation ----------
  void _onTick() {
    final now = DateTime.now();
    if (!_active) {
      _checkSchedule(now);
    } else if (!_paused) {
      final dt = DemoSpeed.value.toDouble();
      _flowLpm = 18 + sin(now.millisecondsSinceEpoch / 7000) * 0.8 + _rand.nextDouble() * 0.4;
      _elapsed += dt;
      _liters += _flowLpm / 60 * dt;
      _checkCompletion();
    }
    _pushStatus();
  }

  void _checkSchedule(DateTime now) {
    final key = '${now.hour}:${now.minute}';
    for (final c in _cycleList) {
      if (!c.enabled) continue;
      if (c.startHour == now.hour && c.startMinute == now.minute) {
        if (_lastTrigger == '${c.id}@$key') continue;
        _lastTrigger = '${c.id}@$key';
        _start(c.id, 'auto');
        return;
      }
    }
  }

  void _start(int id, String by, {double manualTarget = 0}) {
    _active = true;
    _paused = false;
    _cycleId = id;
    _liters = 0;
    _elapsed = 0;
    _startUnix = _nowEpoch;
    _startedBy = by;
    _manualTarget = manualTarget;
  }

  void _checkCompletion() {
    var done = false;
    if (_cycleId == _manualCycleId) {
      done = _manualTarget > 0 && _liters >= _manualTarget;
      if (done) _liters = _manualTarget;
    } else {
      final c = _cycleList.where((c) => c.id == _cycleId).firstOrNull;
      if (c == null) {
        done = true;
      } else {
        final timeUp = _elapsed >= c.durationMinutes * 60;
        final litersUp = c.targetLiters > 0 && _liters >= c.targetLiters;
        done = switch (c.mode) {
          OperationMode.literBased => litersUp,
          OperationMode.timeBased => timeUp,
          OperationMode.timeWindowLiter => timeUp || litersUp,
        };
        if (litersUp && c.mode != OperationMode.timeBased) _liters = c.targetLiters;
      }
    }
    if (done) _finish('completed');
  }

  void _finish(String reason) {
    if (!_active) return;
    final isManual = _cycleId == _manualCycleId;
    final c = _cycleList.where((c) => c.id == _cycleId).firstOrNull;
    _historyList.add(HistoryEntry(
      timestamp: _startUnix,
      cycleId: _cycleId,
      cycleName: isManual ? 'Manual' : (c?.name ?? 'Cycle $_cycleId'),
      mode: c?.mode ?? OperationMode.literBased,
      litersDelivered: double.parse(_liters.toStringAsFixed(1)),
      durationSeconds: _elapsed.round(),
      status: isManual ? 'manual' : reason,
    ));
    if (_historyList.length > _maxHistory) {
      _historyList.removeRange(0, _historyList.length - _maxHistory);
    }
    _active = false;
    _paused = false;
    _history.add(List.of(_historyList));
    _save();
  }

  void _pushStatus() {
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    _status.add(DeviceStatus(
      deviceId: 'FG1_DEMO',
      firmware: '1.3.2-demo',
      pumpOn: _active && !_paused,
      rtcTime: '${two(now.hour)}:${two(now.minute)}',
      rtcDate: '${two(now.day)}/${two(now.month)}/${now.year}',
      rtcSet: true,
      wifiRssi: -55 - _rand.nextInt(6),
      wifiConnected: true,
      mqttConnected: true,
      cycleActive: _active,
      cyclePaused: _paused,
      cycleId: _active ? _cycleId : 0,
      litersDelivered: _active ? _liters : 0,
      startedBy: _active ? _startedBy : '',
      cycleStartUnix: _active ? _startUnix : 0,
      elapsedSeconds: _active ? _elapsed.round() : 0,
    ));
  }

  // ---------- commands ----------
  @override
  void manualOn() {
    if (_active) _finish('stopped');
    _start(_manualCycleId, 'manual');
    _pushStatus();
  }

  @override
  void manualOff() {
    _finish('stopped');
    _pushStatus();
  }

  @override
  void manualLiters(double liters) {
    if (_active) _finish('stopped');
    _start(_manualCycleId, 'manual', manualTarget: liters);
    _pushStatus();
  }

  @override
  void stopCycle() {
    _finish('stopped');
    _pushStatus();
  }

  @override
  void pauseCycle() {
    if (_active) _paused = true;
    _pushStatus();
  }

  @override
  void resumeCycle() {
    _paused = false;
    _pushStatus();
  }

  @override
  void getHistory() => _history.add(List.of(_historyList));

  @override
  void getHistoryRange(DateTime from, DateTime to) {
    final f = _epoch(from), t = _epoch(to);
    _history.add(_historyList.where((h) => h.timestamp >= f && h.timestamp <= t).toList());
  }

  @override
  void getCycles() => _cycles.add(List.of(_cycleList));

  @override
  void setCycles(List<Cycle> cycles) {
    _cycleList = List.of(cycles);
    _cycles.add(List.of(_cycleList));
    _save();
  }

  @override
  void sendRaw(Map<String, dynamic> payload) {}

  @override
  void dispose() {
    _tick?.cancel();
    _status.close();
    _cycles.close();
    _history.close();
    _connected.close();
  }
}
