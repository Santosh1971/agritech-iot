import 'dart:async';
import 'dart:convert';

import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'lan_finder.dart';
import 'local_link.dart';
import 'mqtt_link.dart';
import 'node_link.dart';

/// A node the user added: reachable directly (host) and/or through MQTT on
/// the AgriSense broker (agrisense/WM2/DEVICE_ID/) or Girish broker
/// (FARM/wm2/ZONE/NODE/).
class NodeEntry {
  String name;
  String host; // IP, or 192.168.4.1 on SoftAP; empty = MQTT only
  String lanHost; // the node's farm-WiFi IP, learnt from its state (tried first)
  String broker; // 'agrisense' | 'girish'
  String agId; // WM2_A98803E8 (filled in automatically on first local connect)
  String farm, zone, node;
  int position; // order along the zone's main pipe (1 = first)
  NodeEntry(
      {required this.name,
      this.position = 1,
      this.host = '',
      this.lanHost = '',
      this.broker = 'agrisense',
      this.agId = '',
      this.farm = 'girish',
      this.zone = 'z1',
      this.node = 'n1'});

  String get base => broker == 'girish' ? '$farm/wm2/$zone/$node/' : 'agrisense/WM2/$agId/';
  Map<String, dynamic> toJson() =>
      {'name': name, 'host': host, 'lanHost': lanHost, 'broker': broker, 'agId': agId, 'farm': farm, 'zone': zone, 'node': node, 'position': position};
  factory NodeEntry.fromJson(Map<String, dynamic> j) => NodeEntry(
      name: j['name'] ?? 'Node',
      host: j['host'] ?? '',
      lanHost: j['lanHost'] ?? '',
      broker: j['broker'] ?? 'agrisense',
      agId: j['agId'] ?? '',
      farm: j['farm'] ?? 'girish',
      zone: j['zone'] ?? 'z1',
      node: j['node'] ?? 'n1',
      position: j['position'] ?? 1);
}

/// How a zone is laid out: main pipe down the centre (V1 one side, V2 the
/// other) or along the left/right edge (V1 and V2 both going the same way).
class ZoneInfo {
  String key; // matches NodeEntry.zone, e.g. z1
  String name;
  String layout; // 'center' | 'left' | 'right'
  ZoneInfo({required this.key, required this.name, this.layout = 'center'});
  Map<String, dynamic> toJson() => {'key': key, 'name': name, 'layout': layout};
  factory ZoneInfo.fromJson(Map<String, dynamic> j) =>
      ZoneInfo(key: j['key'] ?? 'z1', name: j['name'] ?? 'Zone', layout: j['layout'] ?? 'center');
}

class NodeStore extends ChangeNotifier {
  List<NodeEntry> nodes = [];

  BrokerSettings brokerFor(NodeEntry e) => e.broker == 'girish' ? BrokerSettings.girish : BrokerSettings.agrisense;
  final Map<String, ZoneInfo> zones = {};

  ZoneInfo zone(String key) => zones.putIfAbsent(key, () {
        final digits = RegExp(r'\d+').firstMatch(key)?.group(0);
        return ZoneInfo(key: key, name: digits != null ? 'Zone $digits' : key);
      });

  /// Next free zone key: z1, z2, ...
  String nextZoneKey() {
    var i = 1;
    while (zones.containsKey('z$i') || nodes.any((n) => n.zone == 'z$i')) {
      i++;
    }
    return 'z$i';
  }

  /// Zones in use, sorted by key, each with its nodes sorted by position.
  List<MapEntry<ZoneInfo, List<NodeEntry>>> layout() {
    final keys = {...zones.keys, ...nodes.map((n) => n.zone)}.toList()
      ..sort((a, b) {
        int n(String k) => int.tryParse(RegExp(r'\d+').firstMatch(k)?.group(0) ?? '') ?? 9999;
        final c = n(a).compareTo(n(b));
        return c != 0 ? c : a.compareTo(b);
      });
    return [
      for (final k in keys)
        MapEntry(zone(k), nodes.where((n) => n.zone == k).toList()..sort((a, b) => a.position.compareTo(b.position)))
    ];
  }

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString('nodes');
    if (raw != null) {
      nodes = (jsonDecode(raw) as List).map((e) => NodeEntry.fromJson(Map<String, dynamic>.from(e))).toList();
    }
    final z = p.getString('zones');
    if (z != null) {
      for (final e in (jsonDecode(z) as List)) {
        final zi = ZoneInfo.fromJson(Map<String, dynamic>.from(e));
        zones[zi.key] = zi;
      }
    }
    notifyListeners();
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString('nodes', jsonEncode(nodes.map((n) => n.toJson()).toList()));
    await p.setString('zones', jsonEncode(zones.values.map((z) => z.toJson()).toList()));
    notifyListeners();
  }
}

/// Owns the active link to one node: tries the direct link first (works with
/// no internet), then MQTT. Keeps the latest state and a short event list.
class NodeController extends ChangeNotifier {
  NodeController(this.entry, this.store) {
    // Android may kill the app's sockets while it is in the background
    // (WhatsApp open, screen off) without telling us; check on return and
    // keep a watchdog so a dead link is replaced instead of hanging.
    _life = AppLifecycleListener(onResume: checkAlive);
    _watch = Timer.periodic(const Duration(seconds: 5), (_) => _watchdog());
  }

  final NodeEntry entry;
  final NodeStore store;
  BrokerSettings get broker => store.brokerFor(entry);
  NodeLink? link;
  Map<String, dynamic>? state;
  final List<Map<String, dynamic>> events = [];
  bool connecting = false;
  String status = 'not connected';
  final List<StreamSubscription> _subs = [];
  bool _clockSent = false;
  bool _disposed = false;
  late final AppLifecycleListener _life;
  late final Timer _watch;
  DateTime _lastRx = DateTime.now(); // last state/event from the node
  DateTime? _downSince; // link missing or disconnected since
  bool _probing = false;

  /// The node can be reached: directly, or through the cloud with the node
  /// itself online. The phone merely reaching the broker does not count.
  bool get connected {
    final l = link;
    if (l == null || !l.isConnected) return false;
    return l is! MqttLink || l.nodeOnline;
  }

  String _statusFor(NodeLink l, bool up) {
    if (!up) return 'reconnecting (${l.kind})...';
    if (l is MqttLink && !l.nodeOnline) return 'node offline (not connected to the cloud)';
    return l is MqttLink ? 'connected (cloud)' : 'connected (${l.kind})';
  }

  /// After the app comes back to the foreground: ask the node for its state;
  /// if the link does not answer, reconnect from scratch.
  Future<void> checkAlive() async {
    if (_disposed || connecting || _probing) return;
    final l = link;
    if (l == null || !l.isConnected) return connect();
    _probing = true;
    final r = await l.send({'cmd': 'get_state'},
        timeout: Duration(seconds: l is LocalLink ? 5 : 12));
    _probing = false;
    if (_disposed || link != l) return;
    if (r['ok'] == true && r['data'] is Map) {
      _onState(Map<String, dynamic>.from(r['data']));
    } else {
      status = 'reconnecting...';
      _notify();
      await connect();
    }
  }

  void _watchdog() {
    if (_disposed || connecting || _probing) return;
    final now = DateTime.now();
    final l = link;
    if (l == null || !l.isConnected) {
      _downSince ??= now;
      if (now.difference(_downSince!) > const Duration(seconds: 15)) connect();
      return;
    }
    _downSince = null;
    // A local node pushes its state every 2 s: 10 s of silence is a dead socket.
    if (l is LocalLink && now.difference(_lastRx) > const Duration(seconds: 10)) connect();
  }

  Future<void> connect() async {
    if (connecting || _disposed) return;
    connecting = true;
    _downSince = null;
    status = 'connecting...';
    _notify();
    _drop();
    // Direct link: the farm-WiFi address the node last reported, then the
    // saved address (often 192.168.4.1, its SoftAP).
    final hosts = {entry.lanHost.trim(), entry.host.trim()}..remove('');
    for (final h in hosts) {
      if (await _tryLocal(h)) return;
    }
    // Neither answered: the node may have a new address on the farm WiFi.
    if (entry.agId.isNotEmpty) {
      status = 'looking for the node on this WiFi...';
      _notify();
      final ip = await findNodeOnLan(entry.agId);
      if (ip != null && await _tryLocal(ip)) {
        entry.lanHost = ip;
        store.save();
        return;
      }
    }
    if (broker.isSet && (entry.broker == 'girish' || entry.agId.isNotEmpty)) {
      final m = MqttLink(broker, entry.base);
      if (await m.connect()) {
        _use(m);
        connecting = false;
        final r = await m.send({'cmd': 'get_state'}, timeout: const Duration(seconds: 70));
        if (r['ok'] == true && r['data'] is Map) _onState(Map<String, dynamic>.from(r['data']));
        return;
      }
      m.dispose();
    }
    connecting = false;
    status = entry.host.isEmpty && !broker.isSet
        ? 'no address: set a local address or the ${entry.broker} broker'
        : entry.host.isEmpty && entry.broker == 'agrisense' && entry.agId.isEmpty
            ? 'set the node\'s AgriSense device ID (WM2_xxxxxxxx)'
            : 'node not reachable: not found on this WiFi'
                '${broker.isSet ? '' : ', and no ${entry.broker} broker login set in the app'}';
    _notify();
  }

  Future<bool> _tryLocal(String h) async {
    final l = LocalLink(h);
    if (await l.connect()) {
      _use(l);
      connecting = false;
      return true;
    }
    l.dispose();
    return false;
  }

  void _use(NodeLink l) {
    link = l;
    status = _statusFor(l, true);
    _lastRx = DateTime.now();
    _subs.add(l.state.listen(_onState));
    _subs.add(l.events.listen((e) {
      _lastRx = DateTime.now();
      events.insert(0, e);
      if (events.length > 50) events.removeLast();
      _notify();
    }));
    _subs.add(l.connected.listen((c) {
      status = _statusFor(l, c);
      _notify();
    }));
    _notify();
    if (l is LocalLink) {
      // Learn the node's AgriSense ID and broker choice so MQTT works away from the farm.
      send({'cmd': 'device_info'}).then((r) {
        final d = r['data'];
        if (r['ok'] == true && d is Map) {
          final id = '${d['device_id'] ?? ''}';
          final b = '${d['broker'] ?? entry.broker}';
          if (id.isNotEmpty && (id != entry.agId || b != entry.broker)) {
            entry
              ..agId = id
              ..broker = b;
            store.save();
          }
        }
      });
    }
    send({'cmd': 'get_events', 'limit': 30}).then((r) {
      final list = (r['data']?['events'] as List?) ?? [];
      events
        ..clear()
        ..addAll(list.reversed.map((e) => Map<String, dynamic>.from(e)));
      _notify();
    });
  }

  void _onState(Map<String, dynamic> s) {
    _lastRx = DateTime.now();
    state = s;
    // Remember the node's farm-WiFi address for next time.
    final ip = '${(s['wifi'] as Map?)?['ip'] ?? ''}';
    if (ip.isNotEmpty && ip != entry.lanHost) {
      entry.lanHost = ip;
      store.save();
    }
    // A node whose clock was never set gets the phone's time once.
    if (s['time_ok'] == false && !_clockSent) {
      _clockSent = true;
      send({'cmd': 'rtc_sync', 'epoch': DateTime.now().millisecondsSinceEpoch ~/ 1000});
    }
    _notify();
  }

  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd, {Duration? timeout}) {
    final l = link;
    if (l == null) return Future.value({'ok': false, 'err': 'not connected'});
    return timeout == null ? l.send(cmd) : l.send(cmd, timeout: timeout);
  }

  void _drop() {
    for (final s in _subs) {
      s.cancel();
    }
    _subs.clear();
    link?.dispose();
    link = null;
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _watch.cancel();
    _life.dispose();
    _drop();
    super.dispose();
  }
}
