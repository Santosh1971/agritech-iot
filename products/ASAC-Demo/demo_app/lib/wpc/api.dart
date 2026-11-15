import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../common/demo.dart';

/// Demo stand-in for the WPC app's WpcApi: same static calls, answered by
/// [WpcSim], a pretend Master + Pump Nodes living in the phone.
class WpcApi {
  static WpcSim get _s => WpcSim.instance;
  static Future<void> _ok() => Future.delayed(const Duration(milliseconds: 250));

  static Future<Map<String, dynamic>> getStatus() async {
    _s.start();
    return _s.status();
  }

  static Future<Map<String, dynamic>> getLocalStatus() => getStatus();

  static Future<void> setDebounceMs(int ms) {
    _s.debounceMs = ms;
    _s.save();
    return _ok();
  }

  static Future<void> setTxPower(int dbm) {
    _s.txPower = dbm;
    _s.save();
    return _ok();
  }

  static Future<void> setNumLevels(int n) {
    _s.numLevels = n.clamp(1, 3);
    _s.save();
    return _ok();
  }

  static Future<void> forgetPump(int slot) {
    _s.pumps.removeWhere((p) => p.slot == slot);
    _s.save();
    return _ok();
  }

  static Future<void> setPumpName(int slot, String name) {
    _s.bySlot(slot)?.name = name;
    _s.save();
    return _ok();
  }

  static Future<void> setPumpLevel(int slot, int level, bool assigned) {
    final p = _s.bySlot(slot);
    if (p != null) {
      if (assigned) {
        p.levels.add(level);
      } else {
        p.levels.remove(level);
      }
    }
    _s.save();
    return _ok();
  }

  static Future<void> setPumpOverride(int slot, bool enabled, {bool? state}) {
    final p = _s.bySlot(slot);
    if (p != null) {
      p.overrideOn = enabled;
      if (state != null) p.overrideState = state;
    }
    return _ok();
  }

  // ---- Pump Node (the "new pump" the installer is standing next to) ----
  static Future<Map<String, dynamic>> getPumpInfo() async {
    _s.start();
    return _s.newPumpInfo();
  }

  static Future<void> setPumpConfig({int? pumpId, String? targetMasterId}) {
    if (targetMasterId != null) _s.provision(targetMasterId);
    return _ok();
  }

  static Future<void> setPumpTxPower(int dbm) {
    _s.newPumpTx = dbm;
    return _ok();
  }

  static Future<void> forgetPumpMaster() {
    _s.newPumpTarget = '';
    return _ok();
  }
}

class SimPump {
  int slot;
  int pumpId;
  String name;
  Set<int> levels;
  bool relay = false;
  bool desired = false;
  bool powerOk = true;
  bool overrideOn = false;
  bool overrideState = false;
  double flowAt = 0; // seconds of relay-on before flow is confirmed
  SimPump(this.slot, this.pumpId, this.name, this.levels);

  bool get waterFlow => relay && powerOk && flowAt >= 3;

  Map<String, dynamic> toJson() =>
      {'slot': slot, 'pumpId': pumpId, 'name': name, 'levels': levels.toList()};
  factory SimPump.fromJson(Map<String, dynamic> j) => SimPump(
      j['slot'] as int, j['pumpId'] as int, j['name'] as String,
      ((j['levels'] as List?) ?? []).map((e) => e as int).toSet());
}

/// Follows the Master firmware (firmware/master_node/src/main.cpp):
/// a sump with up to 3 float switches; a pump assigned to a level runs
/// while the water hasn't reached that level; manual override wins;
/// pumps switching on are started a few seconds apart.
class WpcSim extends ChangeNotifier {
  static final WpcSim instance = WpcSim._();
  WpcSim._();

  static const masterId = 'A5C0DE42';
  static const _key = 'wpc_demo_v1';

  final _rand = Random();
  Timer? _timer;
  bool _loaded = false;

  int numLevels = 3;
  int debounceMs = 2000;
  int txPower = 14;
  List<SimPump> pumps = [];
  bool masterPowerOk = true;

  /// Sump water, 0..100 %.
  double sump = 40;

  /// How much water the farm is drawing: 0 low, 1 normal, 2 high.
  int usage = 1;

  final List<bool> _float = [false, false, false];
  double _sinceStart = 99; // seconds since the last pump was switched on

  // The pump the installer is "connected to" on the Provision tab.
  String newPumpTarget = '';
  int newPumpTx = 14;
  bool _newPumpJoining = false;

  void changed() => notifyListeners();

  SimPump? bySlot(int slot) => pumps.where((p) => p.slot == slot).firstOrNull;

  void start() {
    if (!_loaded) {
      _loaded = true;
      _load();
    }
    _timer ??= Timer.periodic(const Duration(seconds: 1), (_) => _tick());
  }

  Future<void> _load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString(_key);
    if (raw != null) {
      try {
        final j = jsonDecode(raw) as Map<String, dynamic>;
        numLevels = j['numLevels'] as int;
        debounceMs = j['debounceMs'] as int;
        txPower = j['txPower'] as int;
        pumps = (j['pumps'] as List).map((e) => SimPump.fromJson(e as Map<String, dynamic>)).toList();
        notifyListeners();
        return;
      } catch (_) {}
    }
    _seed();
  }

  void _seed() {
    numLevels = 3;
    debounceMs = 2000;
    txPower = 14;
    sump = 40;
    pumps = [
      SimPump(0, 1, 'Borewell North', {1}),
      SimPump(1, 2, 'Borewell South', {2}),
      SimPump(2, 3, 'River pump', {3}),
      SimPump(3, 4, 'Canal pump', {3}),
    ];
    newPumpTarget = '';
    _newPumpJoining = false;
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(
        _key,
        jsonEncode({
          'numLevels': numLevels,
          'debounceMs': debounceMs,
          'txPower': txPower,
          'pumps': pumps.map((e) => e.toJson()).toList(),
        }));
    notifyListeners();
  }

  Future<void> reset() async {
    _seed();
    masterPowerOk = true;
    usage = 1;
    for (var i = 0; i < 3; i++) {
      _float[i] = false;
    }
    await save();
  }

  double threshold(int lvl) => lvl * 100 / (numLevels + 1);

  void _tick() {
    final speed = DemoSpeed.value / 10.0;
    // Water in from every pump whose water is actually flowing, out to the farm.
    final flowing = pumps.where((p) => p.waterFlow).length;
    final out = const [0.8, 1.8, 3.0][usage];
    sump = (sump + (flowing * 1.2 - out) * speed + (_rand.nextDouble() - 0.5) * 0.2).clamp(0, 100);

    // Float switches with a little hysteresis, like a real float.
    for (var i = 0; i < 3; i++) {
      final t = threshold(i + 1);
      if (!_float[i] && sump >= t + 2) _float[i] = true;
      if (_float[i] && sump < t - 2) _float[i] = false;
    }

    // Level logic (applyLevelLogic in the firmware).
    for (final p in pumps) {
      if (p.overrideOn) {
        p.desired = p.overrideState;
      } else {
        p.desired = p.levels.any((l) => l <= numLevels && !_float[l - 1]);
      }
    }

    // Stagger starts: at most one pump switches ON every 3 seconds.
    _sinceStart += 1;
    for (final p in pumps) {
      if (p.desired && !p.relay) {
        if (_sinceStart >= 3) {
          p.relay = true;
          p.flowAt = 0;
          _sinceStart = 0;
        }
      } else if (!p.desired && p.relay) {
        p.relay = false;
        p.flowAt = 0;
      }
      if (p.relay && p.powerOk) p.flowAt += 1;
    }
    notifyListeners();
  }

  Map<String, dynamic> status() => {
        'masterId': '0x$masterId',
        'fw': 'wpc-1.1.0-demo',
        'numLevels': numLevels,
        'debounceMs': debounceMs,
        'txPower': txPower,
        'wifi': {'configured': true, 'ssid': 'FarmWiFi', 'connected': true, 'state': 'connected'},
        'cloud': true,
        'levels': [for (var i = 0; i < numLevels; i++) _float[i]],
        'powerOk': masterPowerOk,
        'noPower': !masterPowerOk,
        'pumps': [
          for (final p in pumps)
            {
              'slot': p.slot,
              'pumpId': p.pumpId,
              'online': true,
              'relay': p.relay,
              'desired': p.desired,
              'assignedLevels': (p.levels.toList()..sort()),
              'name': p.name,
              'waterFlow': p.waterFlow,
              'powerOk': p.powerOk,
              'override': {'enabled': p.overrideOn, 'state': p.overrideState},
            }
        ],
      };

  // ---------------- new pump provisioning ----------------
  int get _nextPumpId => pumps.fold(0, (m, p) => max(m, p.pumpId)) + 1;

  Map<String, dynamic> newPumpInfo() {
    final joined = newPumpTarget == masterId && pumps.any((p) => p.name == 'New pump');
    return {
      'pumpId': joined ? pumps.firstWhere((p) => p.name == 'New pump').pumpId : _nextPumpId,
      'targetMasterId': newPumpTarget.isEmpty ? '' : '0x$newPumpTarget',
      'hasMaster': newPumpTarget.isNotEmpty,
      'joined': joined,
      'relay': false,
      'powerOk': true,
      'waterFlow': false,
      'txPower': newPumpTx,
      'fw': 'wpc-pump-1.1.0-demo',
    };
  }

  void provision(String target) {
    newPumpTarget = target.toUpperCase();
    if (newPumpTarget != masterId || _newPumpJoining) return;
    if (pumps.any((p) => p.name == 'New pump')) return;
    _newPumpJoining = true;
    // Joins over LoRa a few seconds later, unassigned until given a level.
    Timer(const Duration(seconds: 4), () {
      _newPumpJoining = false;
      final slot = pumps.fold(-1, (m, p) => max(m, p.slot)) + 1;
      pumps.add(SimPump(slot, _nextPumpId, 'New pump', {}));
      save();
    });
  }
}
