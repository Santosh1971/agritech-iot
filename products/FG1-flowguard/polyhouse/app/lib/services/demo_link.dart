import 'dart:async';
import 'dart:math';

import 'node_link.dart';

/// A simulated controller (same JSON as the firmware) so the app can be
/// shown and tested without hardware. Climate swings over ~10 minutes so
/// misting starts and stops while you watch.
class DemoLink implements NodeLink {
  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final _conn = StreamController<bool>.broadcast();
  Timer? _t;
  final _rnd = Random(7);
  final DateTime _boot = DateTime.now();

  // settings (firmware defaults)
  final Map<String, dynamic> s = {
    't_start': 32.0, 't_stop': 29.0, 'rh_low': 45.0, 'rh_high': 60.0,
    'on_s': 120, 'off_s': 180, 'win_on': false, 'win_start': 660, 'win_end': 900,
    'max_starts': 15, 'manual_max': 30, 't_hyst': 1.0, 'rh_hyst': 3.0, 'sensor_timeout': 120, 'rev': 0,
  };
  String mode = 'auto';
  bool manualWant = false;
  bool pump = false;
  bool rhStopped = false, tStopped = false; // restart margins, as in the firmware
  bool demand = false;
  String reason = 'idle';
  int onAt = 0, offAt = 0;
  int phaseLeft = 0;
  final List<int> starts = [];
  int startsToday = 0, onSecToday = 0;
  bool localControl = false;
  final List<Map<String, dynamic>> evs = [];

  final List<Map<String, dynamic>> nodes = [
    {'mac': 'A0B1C2D3E401', 'id': 'FG1N_E401', 'name': 'House centre', 'control': true, 'off': 0.0, 'rssi': -61},
    {'mac': 'A0B1C2D3E402', 'id': 'FG1N_E402', 'name': 'North end', 'control': false, 'off': -0.8, 'rssi': -74},
  ];
  final List<Map<String, dynamic>> disc = [
    {'mac': 'A0B1C2D3E4A7', 'id': 'FG1N_E4A7', 'rssi': -70, 'fw': '0.1.0', 'pending': false},
  ];

  int get _now => DateTime.now().difference(_boot).inSeconds + 400;
  double get _baseT => 31 + 3.2 * sin(_now / 95.0);
  double get _baseRh => 52 - 9 * sin(_now / 95.0 + 0.3) + (pump ? 4 : 0);

  void _log(String m) {
    evs.add({'ts': _wall(), 'up': _now, 'msg': m});
    if (evs.length > 60) evs.removeAt(0);
  }

  int _wall() {
    final n = DateTime.now();
    return DateTime.utc(n.year, n.month, n.day, n.hour, n.minute, n.second).millisecondsSinceEpoch ~/ 1000;
  }

  @override
  String get kind => 'Demo';
  @override
  Stream<Map<String, dynamic>> get state => _state.stream;
  @override
  Stream<Map<String, dynamic>> get events => _events.stream;
  @override
  Stream<bool> get connected => _conn.stream;
  @override
  bool get isConnected => true;

  @override
  Future<bool> connect() async {
    _log('Power on — demo controller');
    _t = Timer.periodic(const Duration(seconds: 1), (_) => _step());
    Future.microtask(_push);
    return true;
  }

  (bool, double, double, int, int) _control() {
    double st = 0, srh = 0;
    int used = 0, sel = 0;
    for (final n in nodes) {
      if (n['control'] == true) {
        sel++;
        st += _baseT + (n['off'] as double);
        srh += _baseRh - (n['off'] as double) * 2;
        used++;
      }
    }
    if (localControl || sel == 0) {
      st += _baseT + 1.5;
      srh += _baseRh - 6;
      used++;
      if (localControl) sel++;
    }
    return (used > 0, st / used, srh / used, used, sel);
  }

  void _set(bool on) {
    if (on == pump) return;
    pump = on;
    final (_, t, rh, _, _) = _control();
    if (on) {
      onAt = _now;
      starts.add(_now);
      startsToday++;
    } else {
      offAt = _now;
      onSecToday += _now - onAt;
    }
    _log('Pump ${on ? 'ON' : 'OFF'} ($reason) T ${t.toStringAsFixed(1)} RH ${rh.toStringAsFixed(0)}%');
  }

  void _step() {
    final now = _now;
    final (ok, t, rh, _, _) = _control();
    phaseLeft = 0;
    String climate = '';
    if (!ok) {
      demand = false;
      climate = 'sensor_fault';
    } else if (rh >= s['rh_high'] || (rhStopped && rh > s['rh_high'] - s['rh_hyst'])) {
      rhStopped = true;
      demand = false;
      climate = 'stop_rh_high';
    } else if (t <= s['t_stop'] || (tStopped && t < s['t_stop'] + s['t_hyst'])) {
      rhStopped = false;
      tStopped = true;
      demand = false;
      climate = 'stop_temp_low';
    } else {
      rhStopped = false;
      tStopped = false;
      if (t >= s['t_start'] || rh <= s['rh_low']) demand = true;
    }
    if (mode == 'off') {
      reason = 'mode_off';
      _set(false);
    } else if (mode == 'manual') {
      reason = manualWant ? 'manual_on' : 'manual_off';
      _set(manualWant);
    } else if (climate.isNotEmpty) {
      reason = climate;
      _set(false);
    } else if (!demand) {
      reason = 'idle';
      _set(false);
    } else if (pump) {
      final onFor = now - onAt;
      if (onFor >= s['on_s']) {
        reason = 'misting_gap';
        _set(false);
        phaseLeft = s['off_s'];
      } else {
        reason = 'misting_on';
        phaseLeft = s['on_s'] - onFor;
      }
    } else {
      final offFor = now - offAt;
      starts.removeWhere((x) => now - x >= 3600);
      if (offFor < s['off_s']) {
        reason = 'misting_gap';
        phaseLeft = s['off_s'] - offFor;
      } else if (s['max_starts'] > 0 && starts.length >= s['max_starts']) {
        reason = 'start_limit';
      } else {
        reason = 'misting_on';
        _set(true);
        phaseLeft = s['on_s'];
      }
    }
    _push();
  }

  void _push() {
    final now = _now;
    final (ok, t, rh, used, sel) = _control();
    final n = DateTime.now();
    _state.add({
      'device_id': 'FG1P_DEMO',
      'fw': '0.1.0',
      'uptime': now,
      'clock_ok': true,
      'rtc_chip': true,
      'time': '${n.hour.toString().padLeft(2, '0')}:${n.minute.toString().padLeft(2, '0')}:${n.second.toString().padLeft(2, '0')}',
      'pump_on': pump,
      'mode': mode,
      'reason': reason,
      'demand': demand,
      'in_window': true,
      'phase_left': phaseLeft,
      'starts_hr': starts.where((x) => now - x < 3600).length,
      'starts_today': startsToday,
      'on_min_today': (onSecToday + (pump ? now - onAt : 0)) ~/ 60,
      'control': {'ok': ok, 't': t, 'rh': rh, 'used': used, 'selected': sel, 'fallback': sel == 0},
      'outside': {'ok': true, 'sensor': 'SHT4x', 't': _baseT + 1.5, 'rh': _baseRh - 6},
      'local_control': localControl,
      'nodes': [
        for (final x in nodes)
          {
            'mac': x['mac'], 'id': x['id'], 'name': x['name'], 'control': x['control'],
            'online': true, 'reset': false, 'age': _rnd.nextInt(10), 'ok': true,
            't': _baseT + (x['off'] as double), 'rh': _baseRh - (x['off'] as double) * 2,
            'rssi': (x['rssi'] as int) + _rnd.nextInt(5) - 2, 'rssi_node': (x['rssi'] as int) - 3,
            'rx': now ~/ 10, 'lost': (x['rssi'] as int) < -70 ? now ~/ 400 : 0,
            'fw': '0.1.0', 'up': now, 'sensor': 1,
          }
      ],
      'discovered': disc,
      'wifi': {'connected': true, 'ssid': 'Farm-WiFi', 'rssi': -58, 'mode': 'cloud', 'ap': '', 'ip': '192.168.1.40', 'channel': 6},
      'mqtt': true,
      'settings': s,
    });
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd, {Duration timeout = const Duration(seconds: 10)}) async {
    await Future.delayed(const Duration(milliseconds: 150));
    Object data = {};
    String? err;
    String mac = cmd['mac'] ?? '';
    switch (cmd['cmd']) {
      case 'set_mode':
        mode = cmd['mode'];
        if (mode == 'manual') manualWant = pump;
        _log('Mode: $mode');
      case 'pump_on':
        mode = 'manual';
        manualWant = true;
        _log('Manual: pump ON');
      case 'pump_off':
        mode = 'manual';
        manualWant = false;
        _log('Manual: pump OFF');
      case 'set_settings':
        for (final k in s.keys) {
          if (cmd.containsKey(k)) s[k] = cmd[k];
        }
        _log('Settings changed');
      case 'node_add':
        final d = disc.where((x) => x['mac'] == mac).toList();
        if (d.isEmpty) {
          err = 'sensor not seen recently';
        } else {
          d.first['pending'] = true;
          Future.delayed(const Duration(seconds: 3), () {
            disc.removeWhere((x) => x['mac'] == mac);
            nodes.add({'mac': mac, 'id': d.first['id'], 'name': '', 'control': false, 'off': 0.4, 'rssi': -70});
            _log('Sensor ${d.first['id']} added');
          });
        }
      case 'node_remove':
        nodes.removeWhere((x) => x['mac'] == mac);
        _log('Sensor removed');
      case 'node_rename':
        for (final x in nodes) {
          if (x['mac'] == mac) x['name'] = cmd['name'];
        }
      case 'set_control':
        final src = (cmd['sources'] as List).cast<String>();
        localControl = src.contains('local');
        for (final x in nodes) {
          x['control'] = src.contains(x['mac']);
        }
        _log('Control input changed');
      case 'get_events':
        data = evs;
      case 'get_trend':
        final end = _wall() ~/ 300 * 300;
        data = [
          for (int i = 287; i >= 0; i--)
            () {
              final ts = end - i * 300;
              final h = (ts % 86400) / 3600.0;
              final day = max(0.0, sin((h - 7) / 12 * pi));
              final tt = 22 + 12 * day + _rnd.nextDouble();
              final rr = 78 - 35 * day + _rnd.nextDouble() * 3;
              final p = (h >= 11 && h < 15 && tt > 31) ? 120 : 0;
              return [ts, (tt * 10).round(), (rr * 10).round(), ((tt + 1.5) * 10).round(), ((rr - 6) * 10).round(), p];
            }()
        ];
      case 'wifi_scan':
        Future.delayed(const Duration(seconds: 2), () {
          _events.add({
            'type': 'wifi_scan_result',
            'data': [
              {'ssid': 'Farm-WiFi', 'rssi': -58, 'open': false},
              {'ssid': 'JioFiber-2G', 'rssi': -77, 'open': false},
            ]
          });
        });
        data = {'scanning': true};
      default:
        break;
    }
    _push();
    return {'ok': err == null, 'err': err, 'cmd': cmd['cmd'], 'data': data};
  }

  @override
  void dispose() {
    _t?.cancel();
    _state.close();
    _events.close();
    _conn.close();
  }
}
