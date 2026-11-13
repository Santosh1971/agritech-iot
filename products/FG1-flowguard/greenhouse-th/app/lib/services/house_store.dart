import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'demo_link.dart';
import 'local_link.dart';
import 'mqtt_link.dart';
import 'node_link.dart';

/// One greenhouse = one FG1 Controller (its pump + its sensor nodes).
class House {
  String name;
  String deviceId; // FG1P_xxxx — filled in on first local connect if unknown
  String host; // 192.168.4.1 on the controller's WiFi, or its farm-LAN IP; '' = internet only
  bool demo;
  House({required this.name, this.deviceId = '', this.host = '192.168.4.1', this.demo = false});

  Map<String, dynamic> toJson() => {'name': name, 'deviceId': deviceId, 'host': host, 'demo': demo};
  factory House.fromJson(Map<String, dynamic> j) => House(
      name: j['name'] ?? 'Greenhouse',
      deviceId: j['deviceId'] ?? '',
      host: j['host'] ?? '192.168.4.1',
      demo: j['demo'] ?? false);
}

class HouseStore extends ChangeNotifier {
  final List<House> houses = [];
  final Map<House, HouseConn> _conns = {};

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString('houses');
    if (raw != null) {
      for (final j in jsonDecode(raw) as List) {
        houses.add(House.fromJson(Map<String, dynamic>.from(j)));
      }
    }
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString('houses', jsonEncode(houses.map((h) => h.toJson()).toList()));
    notifyListeners();
  }

  HouseConn conn(House h) => _conns.putIfAbsent(h, () => HouseConn(h, this)..start());

  Future<void> add(House h) async {
    houses.add(h);
    await save();
  }

  Future<void> remove(House h) async {
    _conns.remove(h)?.dispose();
    houses.remove(h);
    await save();
  }

  /// Rebuild the links after the host / ID changed.
  Future<void> updated(House h) async {
    _conns.remove(h)?.dispose();
    await save();
  }
}

/// Live connection to one controller. Uses the local link when it is up
/// (fast, no internet needed), otherwise the internet link.
class HouseConn extends ChangeNotifier {
  HouseConn(this.house, this.store);

  final House house;
  final HouseStore store;
  LocalLink? _local;
  MqttLink? _cloud;
  DemoLink? _demo;
  final List<StreamSubscription> _subs = [];
  final _events = StreamController<Map<String, dynamic>>.broadcast();

  Map<String, dynamic>? status;
  DateTime? statusAt;
  String statusVia = '';
  bool _disposed = false;
  Timer? _tick;

  Stream<Map<String, dynamic>> get events => _events.stream;

  void start() {
    if (house.demo) {
      _demo = DemoLink();
      _wire(_demo!);
      _demo!.connect();
    } else {
      if (house.host.isNotEmpty) {
        _local = LocalLink(house.host);
        _wire(_local!);
        _subs.add(_local!.connected.listen((up) {
          if (up) _syncClock();
        }));
        _local!.connect();
      }
      _startCloud();
    }
    // Re-evaluate "fresh" once a second for the UI.
    _tick = Timer.periodic(const Duration(seconds: 1), (_) => notifyListeners());
  }

  void _startCloud() {
    if (_cloud != null || house.deviceId.isEmpty || kIsWeb) return;
    _cloud = MqttLink(house.deviceId);
    _wire(_cloud!);
    _cloud!.connect();
  }

  void _wire(NodeLink l) {
    _subs.add(l.state.listen((s) {
      status = s;
      statusAt = DateTime.now();
      statusVia = l.kind;
      final id = s['device_id'];
      if (id is String && id.isNotEmpty && house.deviceId != id) {
        house.deviceId = id;
        store.save();
        _startCloud();
      }
      notifyListeners();
    }));
    _subs.add(l.events.listen(_events.add));
    _subs.add(l.connected.listen((_) => notifyListeners()));
  }

  NodeLink? get active {
    if (_demo != null) return _demo;
    if (_local?.isConnected == true) return _local;
    if (_cloud?.isConnected == true) return _cloud;
    return null;
  }

  /// "Local", "Cloud", "Demo" or "" (offline).
  String get via => active?.kind ?? '';
  bool get online => active != null && fresh;
  bool get fresh => statusAt != null && DateTime.now().difference(statusAt!).inSeconds < 20;

  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd) async {
    final l = active;
    if (l == null) return {'ok': false, 'err': 'Controller not reachable'};
    final r = await l.send(cmd);
    return r;
  }

  /// Device clock = IST wall time encoded as if it were UTC (FG1 convention).
  static int wallEpoch(DateTime t) =>
      DateTime.utc(t.year, t.month, t.day, t.hour, t.minute, t.second).millisecondsSinceEpoch ~/ 1000;

  void _syncClock() {
    _local?.send({'cmd': 'rtc_sync', 'unix': wallEpoch(DateTime.now())});
  }

  Future<Map<String, dynamic>> syncClock() => send({'cmd': 'rtc_sync', 'unix': wallEpoch(DateTime.now())});

  @override
  void notifyListeners() {
    if (!_disposed) super.notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _tick?.cancel();
    for (final s in _subs) {
      s.cancel();
    }
    _local?.dispose();
    _cloud?.dispose();
    _demo?.dispose();
    _events.close();
    super.dispose();
  }
}

/// Convenience reader over the status JSON.
class St {
  St(this.j);
  final Map<String, dynamic> j;

  T _g<T>(Map<String, dynamic>? m, String k, T def) {
    final v = m?[k];
    if (v is T) return v;
    if (T == double && v is num) return v.toDouble() as T;
    if (T == int && v is num) return v.toInt() as T;
    return def;
  }

  Map<String, dynamic> _m(String k) => j[k] is Map ? Map<String, dynamic>.from(j[k]) : {};

  bool get pumpOn => _g(j, 'pump_on', false);
  String get mode => _g(j, 'mode', 'auto');
  String get reason => _g(j, 'reason', '');
  bool get inWindow => _g(j, 'in_window', true);
  int get phaseLeft => _g(j, 'phase_left', 0);
  int get startsHr => _g(j, 'starts_hr', 0);
  int get startsToday => _g(j, 'starts_today', 0);
  int get onMinToday => _g(j, 'on_min_today', 0);
  bool get clockOk => _g(j, 'clock_ok', false);
  String get time => _g(j, 'time', '--:--');
  String get fw => _g(j, 'fw', '');
  String get deviceId => _g(j, 'device_id', '');

  Map<String, dynamic> get control => _m('control');
  bool get controlOk => _g(control, 'ok', false);
  double? get controlT => controlOk ? _g<double>(control, 't', 0) : null;
  double? get controlRh => controlOk ? _g<double>(control, 'rh', 0) : null;
  bool get controlFallback => _g(control, 'fallback', false);
  int get controlUsed => _g(control, 'used', 0);
  int get controlSelected => _g(control, 'selected', 0);

  Map<String, dynamic> get outside => _m('outside');
  bool get outsideOk => _g(outside, 'ok', false);
  double? get outsideT => outsideOk ? _g<double>(outside, 't', 0) : null;
  double? get outsideRh => outsideOk ? _g<double>(outside, 'rh', 0) : null;
  String get outsideSensor => _g(outside, 'sensor', '');
  bool get localControl => _g(j, 'local_control', false);

  List<Map<String, dynamic>> _list(String k) =>
      j[k] is List ? (j[k] as List).map((e) => Map<String, dynamic>.from(e)).toList() : [];
  List<Map<String, dynamic>> get nodes => _list('nodes');
  List<Map<String, dynamic>> get discovered => _list('discovered');

  Map<String, dynamic> get settings => _m('settings');
  Map<String, dynamic> get wifi => _m('wifi');
  bool get mqtt => _g(j, 'mqtt', false);
}

String reasonText(String r) => const {
      'idle': 'Conditions OK — no misting needed',
      'misting_on': 'Misting — pump ON',
      'misting_gap': 'Misting — resting between pulses',
      'stop_temp_low': 'Stopped — too cool for misting',
      'stop_rh_high': 'Stopped — too humid for misting',
      'outside_window': 'Outside working hours',
      'sensor_fault': 'Sensor fault — misting stopped for safety',
      'start_limit': 'Waiting — max pump starts per hour reached',
      'mode_off': 'Misting switched OFF',
      'manual_on': 'Manual — pump ON',
      'manual_off': 'Manual — pump OFF',
    }[r] ??
    r;

String mmss(int s) => '${s ~/ 60}:${(s % 60).toString().padLeft(2, '0')}';
String hhmmMin(int m) => '${(m ~/ 60).toString().padLeft(2, '0')}:${(m % 60).toString().padLeft(2, '0')}';
