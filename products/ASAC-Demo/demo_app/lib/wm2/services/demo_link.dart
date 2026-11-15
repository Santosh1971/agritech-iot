import 'dart:async';
import 'dart:math';
import 'package:flutter/foundation.dart';
import '../../common/demo.dart';
import 'node_link.dart';

const _days = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];

int _now() => DateTime.now().millisecondsSinceEpoch ~/ 1000;
int get _tz => DateTime.now().timeZoneOffset.inSeconds;
int _localDay(int ts) => (ts + _tz) ~/ 86400;
String _wd(int day) => _days[(day + 3) % 7];

/// Farm-wide conditions shared by every simulated node.
class Wm2Farm extends ChangeNotifier {
  static final Wm2Farm instance = Wm2Farm._();
  Wm2Farm._();

  /// Water reaching the nodes (canal turn / main pump running).
  bool water = true;

  final Map<String, Wm2Sim> _nodes = {};
  Timer? _timer;

  Wm2Sim node(String key) {
    _timer ??= Timer.periodic(const Duration(seconds: 1), (_) {
      for (final n in _nodes.values) {
        n.tick();
      }
    });
    return _nodes.putIfAbsent(key, () => Wm2Sim(_nodes.length, key));
  }

  void setWater(bool v) {
    water = v;
    for (final n in _nodes.values) {
      n.event({'event': 'water', 'present': v});
    }
    notifyListeners();
  }

  void reset() {
    water = true;
    for (final n in _nodes.values) {
      n.reset();
    }
    notifyListeners();
  }
}

class _Valve {
  bool open = false;
  String mode = 'program';
  bool manual = false;
  double left = 0; // manual seconds left, 0 = until closed
  bool forceClose = false;
  Map<String, dynamic>? run; // since, sec, l
  Map<String, dynamic> last = {};
  Map<String, dynamic> program = {};
  // Today's progress towards the program target.
  int doneDay = -1;
  double todaySec = 0, todayL = 0;
}

/// One pretend WM2 node: 2 latching valves, 4 flow inputs, pressure switch,
/// battery. Port of firmware/tools/node_sim.py, plus programs that run by
/// themselves when water arrives, like the real node.
class Wm2Sim {
  Wm2Sim(this.idx, this.key) {
    reset();
  }

  final int idx;
  final String key;
  final _rand = Random(7);
  final List<Map<String, dynamic>> events = [];
  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final List<_Valve> valves = [_Valve(), _Valve()];
  final List<double> flowToday = [0, 0, 0, 0];
  bool bypass = false;
  int bypassUntil = 0;
  String get devId => 'WM2_DEMO${(idx + 1).toString().padLeft(4, '0')}';

  void reset() {
    final now = _now();
    bypass = false;
    events.clear();
    for (var i = 0; i < 4; i++) {
      flowToday[i] = 0;
    }
    const daySets = [
      ['mon', 'wed', 'fri'],
      ['tue', 'thu', 'sat'],
      ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'],
      ['wed', 'sat'],
    ];
    const mins = [45, 30, 40, 35];
    for (var v = 0; v < 2; v++) {
      final k = (idx + v) % 4;
      final lastEnd = now - (idx * 2 + v + 1) * 86400;
      valves[v]
        ..open = false
        ..mode = 'program'
        ..manual = false
        ..left = 0
        ..run = null
        ..doneDay = -1
        ..todaySec = 0
        ..todayL = 0
        ..last = {'start': lastEnd - mins[k] * 60, 'end': lastEnd, 'min': mins[k], 'l': (mins[k] * 20.6).roundToDouble()}
        ..program = {
          'enabled': true,
          'type': 'timer',
          // Every node waters today on at least one valve so the demo moves.
          'days': v == 0 ? daySets[2] : daySets[k],
          'minutes': mins[k],
          'window': ['00:00', '23:59'],
          'carry_over': false,
        };
    }
  }

  void push() => _state.add(state());

  Stream<Map<String, dynamic>> get stateStream => _state.stream;
  Stream<Map<String, dynamic>> get eventStream => _events.stream;

  bool get _flowing => Wm2Farm.instance.water || bypass;

  void event(Map<String, dynamic> e) {
    e.addAll({'v': 1, 'ts': _now(), 'node': 'z/$key'});
    events.add(e);
    if (events.length > 200) events.removeAt(0);
    _events.add(e);
  }

  bool _inWindow(Map<String, dynamic> p) {
    final w = (p['window'] as List?) ?? ['00:00', '23:59'];
    int m(String s) {
      final a = s.split(':');
      return int.parse(a[0]) * 60 + int.parse(a[1]);
    }

    final t = DateTime.now();
    final now = t.hour * 60 + t.minute;
    return now >= m('${w[0]}') && now <= m('${w[1]}');
  }

  /// Is this valve's program still owed water today?
  bool _due(int i) {
    final v = valves[i];
    final p = v.program;
    if (v.mode != 'program' || p['enabled'] != true) return false;
    final today = _localDay(_now());
    if (v.doneDay != today) {
      v.doneDay = today;
      v.todaySec = 0;
      v.todayL = 0;
    }
    if (!((p['days'] as List?) ?? []).contains(_wd(today))) return false;
    if (!_inWindow(p)) return false;
    return p['type'] == 'litres'
        ? v.todayL < ((p['litres'] as num?) ?? 0)
        : v.todaySec < ((p['minutes'] as num?) ?? 0) * 60;
  }

  int? _nextDue(int i) {
    final v = valves[i];
    final p = v.program;
    if (v.mode != 'program' || p['enabled'] != true) return null;
    final today = _localDay(_now());
    for (var d = 0; d < 14; d++) {
      if (((p['days'] as List?) ?? []).contains(_wd(today + d))) {
        if (d == 0) {
          if (_due(i)) return _now();
          continue;
        }
        return (today + d) * 86400 - _tz;
      }
    }
    return null;
  }

  void tick() {
    final now = _now();
    final speed = DemoSpeed.value.toDouble();
    if (bypass && now >= bypassUntil) bypass = false;
    for (var i = 0; i < 2; i++) {
      final v = valves[i];
      final due = _due(i);
      // Sequential (firmware default): a program run waits while the
      // node's other valve is open. Manual runs are not held back.
      final otherOpen = valves[1 - i].open;
      final want = (v.manual || (due && v.mode != 'off' && (v.open || !otherOpen))) && !v.forceClose;
      v.forceClose = false;
      if (want && !v.open && _flowing) {
        v.open = true;
        v.run = {'since': now, 'sec': 0.0, 'l': 0.0};
        event({'event': 'valve', 'valve': i + 1, 'open': true, 'reason': v.manual ? 'manual' : 'program'});
      }
      if (v.open && (!want || !_flowing)) {
        v.open = false;
        final r = v.run ?? {'since': now, 'sec': 0.0, 'l': 0.0};
        v.last = {
          'start': r['since'],
          'end': now,
          'min': ((r['sec'] as double) / 60).round(),
          'l': ((r['l'] as double) * 10).round() / 10,
        };
        v.run = null;
        event({
          'event': 'valve',
          'valve': i + 1,
          'open': false,
          'reason': !_flowing ? 'water_lost' : (v.manual ? 'manual' : 'program_done'),
        });
      }
      if (v.open && _flowing) {
        final l = 20.6 / 60 * speed * (0.95 + _rand.nextDouble() * 0.1);
        v.run!['sec'] = (v.run!['sec'] as double) + speed;
        v.run!['l'] = (v.run!['l'] as double) + l;
        v.todaySec += speed;
        v.todayL += l;
        flowToday[2 * i] += l / 2;
        flowToday[2 * i + 1] += l / 2;
        if (v.manual && v.left > 0) {
          v.left -= speed;
          if (v.left <= 0) {
            v.manual = false;
            v.left = 0;
            event({'event': 'manual_done', 'valve': i + 1});
          }
        }
      }
    }
    _state.add(state());
  }

  double _lpm(int input) {
    final v = valves[input < 2 ? 0 : 1];
    if (!(v.open && _flowing)) return 0;
    return ((10.0 + _rand.nextDouble() * 0.8) * 10).round() / 10;
  }

  Map<String, dynamic> state() {
    final now = _now();
    final t = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    final water = Wm2Farm.instance.water;
    return {
      'v': 1,
      'ts': now,
      'time_ok': true,
      'local': '${t.year}-${two(t.month)}-${two(t.day)} ${two(t.hour)}:${two(t.minute)}:${two(t.second)}',
      'node': 'z/$key',
      'fw': '0.1.8-demo',
      'state': water || valves.any((v) => v.open) ? 'active' : 'idle',
      'water': water,
      'ps': water,
      'ps_fitted': true,
      'bench_s': 0,
      'bypass': {'on': bypass, 'left_s': bypass ? max(0, bypassUntil - now) : 0},
      'flow': [
        for (var i = 0; i < 4; i++)
          {
            'in': i + 1,
            'valve': i < 2 ? 1 : 2,
            'lpm': _lpm(i),
            'l_today': (flowToday[i] * 10).round() / 10,
            'l_total': (flowToday[i] * 10).round() / 10 + 8200 + idx * 900,
          }
      ],
      'valves': [
        for (var i = 0; i < 2; i++) _valveJson(i),
      ],
      'batt': {
        'v': 13.21 - idx * 0.08,
        'soc': 78 - idx * 5,
        'trend': 'charging',
        'days': 24.5,
        'level': 'ok',
      },
      'wifi': {'ip': '192.168.1.${50 + idx}', 'rssi': -56 - idx * 3, 'ap': false, 'ap_clients': 0},
      'mqtt': true,
      'alarms': [],
      'uptime_s': now % 100000,
    };
  }

  Map<String, dynamic> _valveJson(int i) {
    final v = valves[i];
    final p = v.program;
    final j = <String, dynamic>{
      'v': i + 1,
      'open': v.open,
      'target': v.open,
      'pending': false,
      'waiting': (v.manual || _due(i)) && !v.open && !_flowing,
      'mode': v.mode,
      'due': _due(i),
      'done': !_due(i) && v.todaySec > 0,
      'planned_min': p['minutes'] ?? 0,
      if (p['type'] == 'litres') 'planned_l': p['litres'] ?? 0,
      'run_min': (v.todaySec / 60).round(),
      'litres': (v.todayL * 10).round() / 10,
      'manual': v.manual,
      'manual_left_s': v.left.round(),
      'never_close': false,
      'prog': p,
      'last': v.last,
    };
    if (v.run != null) {
      j['run'] = {
        'since': v.run!['since'],
        'min': ((v.run!['sec'] as double) / 60).floor(),
        'l': ((v.run!['l'] as double) * 10).round() / 10,
      };
    }
    final nd = _nextDue(i);
    if (nd != null) j['next_due'] = nd;
    return j;
  }

  Map<String, dynamic> _cfg() => {
        'farm': 'demo',
        'zone': 'z1',
        'node': key,
        'ssid': 'FarmWiFi',
        'broker': 'agrisense',
        'ag_id': devId,
        'tz_min': _tz ~/ 60,
        'profile': 'responsive',
        'safe_close': true,
        'bypass_default_min': 60,
        'chemistry': 'lifepo4',
        'capacity_ah': 18,
        'v_warn': 12.8,
        'v_save': 12.5,
        'v_crit': 12.0,
        'sequential': true,
        'ps_fitted': true,
        'flow': [for (var i = 0; i < 4; i++) {'input': i + 1, 'valve': i < 2 ? 1 : 2, 'ppl': 450}],
        'valves': [
          for (var v = 0; v < 2; v++)
            {'valve': v + 1, 'mode': valves[v].mode, 'pulse_ms': 50, 'invert': false, 'never_close': false,
             'on_water_loss': 'keep', 'close_after_min': 10}
        ],
      };

  Map<String, dynamic> dispatch(Map<String, dynamic> req) {
    final cmd = '${req['cmd'] ?? ''}';
    final resp = <String, dynamic>{'v': 1, 'cmd': cmd, 'ok': true};
    if (req['id'] != null) resp['id'] = req['id'];
    var data = <String, dynamic>{};
    final vi = ((req['valve'] as num?)?.toInt() ?? 0) - 1;
    switch (cmd) {
      case 'get_state':
        data = state();
      case 'device_info':
        data = {
          'fw': '0.1.8-demo', 'chip': 'ESP32-D0WD-V3', 'mac': '20:9B:A9:00:00:0$idx', 'flash_mb': 4,
          'fs_used': 40960, 'fs_total': 1441792, 'heap': 180000, 'uptime_s': 1234,
          'host': 'wm2-demo$idx', 'base': 'agrisense/WM2/$devId/', 'broker': 'agrisense',
          'broker_host': 'mqtt.agrisenseandcontrol.in', 'device_id': devId, 'mqtt': true,
          'ip': '192.168.1.${50 + idx}', 'ap': true, 'ap_ssid': 'WM2-DEMO$idx',
        };
      case 'get_config':
        data = _cfg();
      case 'valve_open':
        if (vi < 0 || vi > 1) {
          resp.addAll({'ok': false, 'err': 'valve must be 1 or 2'});
        } else {
          valves[vi]
            ..manual = true
            ..left = ((req['minutes'] as num?) ?? 0) * 60.0;
          data['state'] = _flowing ? 'opening' : 'waiting_for_water';
        }
      case 'valve_close':
        if (vi >= 0 && vi < 2) {
          valves[vi]
            ..manual = false
            ..forceClose = true;
          // A program valve closed by hand counts today's run as done.
          final p = valves[vi].program;
          valves[vi].todaySec = max(valves[vi].todaySec, ((p['minutes'] as num?) ?? 0) * 60.0);
          valves[vi].todayL = max(valves[vi].todayL, ((p['litres'] as num?) ?? 0).toDouble());
        }
      case 'set_mode':
        if (vi >= 0 && vi < 2) valves[vi].mode = '${req['mode'] ?? 'program'}';
      case 'set_program':
        if (vi >= 0 && vi < 2) {
          valves[vi].program = Map<String, dynamic>.from(req['program'] as Map? ?? {});
          data['program'] = valves[vi].program;
        }
      case 'get_program':
        data['programs'] = [
          for (var i = 0; i < 2; i++) {'valve': i + 1, 'mode': valves[i].mode, 'program': valves[i].program}
        ];
      case 'bypass':
        bypass = req['on'] != false;
        bypassUntil = _now() + ((req['minutes'] as num?) ?? 60).toInt() * 60;
        event({'event': 'bypass', 'on': bypass, 'src': 'app'});
      case 'get_events':
        final n = ((req['limit'] as num?) ?? 50).toInt();
        data['events'] = events.length > n ? events.sublist(events.length - n) : List.of(events);
      case 'get_days':
        data['days'] = _days_(((req['days'] as num?) ?? 7).toInt());
      case 'get_history':
        data = _history(req);
      case 'wifi_scan':
        data['networks'] = [
          {'ssid': 'FarmWiFi', 'rssi': -52, 'open': false},
          {'ssid': 'Office', 'rssi': -71, 'open': false},
        ];
      case 'diag_inputs':
        data['inputs'] = [
          for (final n in ['flow1', 'flow2', 'flow3', 'flow4', 'ps']) {'name': n, 'pin_falls': 0, 'low_pct': 0}
        ];
      default:
        // set_config, wifi_config, reboot, rtc_sync, valve_test, bench...:
        // accepted, nothing to simulate.
        break;
    }
    if (data.isNotEmpty) resp['data'] = data;
    return resp;
  }

  // ignore: non_constant_identifier_names
  List<Map<String, dynamic>> _days_(int n) {
    final today = _localDay(_now());
    final out = <Map<String, dynamic>>[];
    for (var d = n; d > 0; d--) {
      for (var v = 0; v < 2; v++) {
        if (!((valves[v].program['days'] as List?) ?? []).contains(_wd(today - d))) continue;
        final planned = ((valves[v].program['minutes'] as num?) ?? 45).toInt();
        final partial = d % 5 == 0;
        final dt = DateTime.fromMillisecondsSinceEpoch((today - d) * 86400 * 1000, isUtc: true);
        out.add({
          'event': 'day_result', 'valve': v + 1, 'day': today - d,
          'date': '${dt.year}-${dt.month.toString().padLeft(2, '0')}-${dt.day.toString().padLeft(2, '0')}',
          'planned_min': planned, 'run_min': partial ? planned * 2 ~/ 3 : planned,
          'litres': [planned * 10.3, planned * 10.1], 'normal_litres': [planned * 10.3, planned * 10.3],
          'water_min': 300, 'status': partial ? 'partial' : 'done', 'alarms': [],
        });
      }
    }
    return out;
  }

  Map<String, dynamic> _history(Map<String, dynamic> req) {
    final now = _now();
    final f = ((req['from'] as num?) ?? now - 86400).toInt();
    final t = ((req['to'] as num?) ?? now).toInt();
    final res = req['res'] as String? ?? (t - f <= 2 * 86400 ? 'raw' : t - f <= 31 * 86400 ? 'hour' : 'day');
    final step = const {'raw': 60, 'hour': 3600, 'day': 86400}[res] ?? 3600;
    final start = step >= 3600 ? f - (f + _tz) % step : f;
    final pts = <List<num>>[];
    final tot = [0.0, 0.0, 0.0, 0.0];
    final om = [0, 0];
    for (var ts = start; ts < t && pts.length < 1500; ts += step) {
      final day = _localDay(ts);
      final wd = _wd(day);
      final hr = ((ts + _tz) ~/ 3600) % 24;
      final on = [for (var x = 0; x < 2; x++) ((valves[x].program['days'] as List?) ?? []).contains(wd)];
      if (res == 'raw') {
        final a = on[0] && (hr == 6 || hr == 7), b = on[1] && hr == 8;
        pts.add([ts, a ? 10.4 : 0, a ? 10.2 : 0, b ? 9.9 : 0, b ? 10.1 : 0, 0, 13.1]);
        if (a) {
          tot[0] += 10.4;
          tot[1] += 10.2;
          om[0] += 1;
        }
        if (b) {
          tot[2] += 9.9;
          tot[3] += 10.1;
          om[1] += 1;
        }
      } else {
        final m = res == 'hour'
            ? [on[0] && hr == 6 ? 45 : 0, on[1] && hr == 8 ? 30 : 0]
            : [for (var x = 0; x < 2; x++) on[x] ? ((valves[x].program['minutes'] as num?) ?? 0).toInt() : 0];
        final l = [m[0] * 10.4, m[0] * 10.2, m[1] * 9.9, m[1] * 10.1];
        pts.add([ts, ...l, m[0], m[1], max(m[0], m[1]), 12.9]);
        for (var i = 0; i < 4; i++) {
          tot[i] += l[i];
        }
        om[0] += m[0];
        om[1] += m[1];
      }
    }
    double r1(double x) => (x * 10).round() / 10;
    return {
      'from': f, 'to': t, 'res': res, 'points': pts,
      'columns': res == 'raw' ? 'ts,lpm1,lpm2,lpm3,lpm4,flags,batt' : 'ts,l1,l2,l3,l4,v1_min,v2_min,water_min,min_batt',
      'totals': {
        'litres': tot.map(r1).toList(),
        'valve_litres': [r1(tot[0] + tot[1]), r1(tot[2] + tot[3])],
        'valve_open_min': om, 'water_min': max(om[0], om[1]), 'min_batt': 12.95, 'records': pts.length,
      },
      'truncated': false,
    };
  }
}

/// NodeLink onto a simulated node, so the WM2 screens work unchanged.
class Wm2DemoLink with ReplyMatcher implements NodeLink {
  Wm2DemoLink(String key) : sim = Wm2Farm.instance.node(key);
  final Wm2Sim sim;
  final _connected = StreamController<bool>.broadcast();

  @override
  String get kind => 'Demo';
  @override
  Stream<Map<String, dynamic>> get state => sim.stateStream;
  @override
  Stream<Map<String, dynamic>> get events => sim.eventStream;
  @override
  Stream<bool> get connected => _connected.stream;
  @override
  bool get isConnected => true;

  @override
  Future<bool> connect() async {
    _connected.add(true);
    return true;
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd,
      {Duration timeout = const Duration(seconds: 8)}) async {
    await Future.delayed(const Duration(milliseconds: 150));
    final r = sim.dispatch(cmd);
    sim.push(); // fresh state right after a command
    return r;
  }

  @override
  void dispose() => _connected.close();
}
