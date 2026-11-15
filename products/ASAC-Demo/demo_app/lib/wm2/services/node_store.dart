import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'demo_link.dart';
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
    final raw = p.getString('wm2_demo_nodes');
    if (raw != null) {
      nodes = (jsonDecode(raw) as List).map((e) => NodeEntry.fromJson(Map<String, dynamic>.from(e))).toList();
    } else {
      _seedDemo();
    }
    final z = p.getString('wm2_demo_zones');
    if (z != null) {
      for (final e in (jsonDecode(z) as List)) {
        final zi = ZoneInfo.fromJson(Map<String, dynamic>.from(e));
        zones[zi.key] = zi;
      }
    }
    notifyListeners();
  }

  /// Sample farm: two zones, two nodes each, 2 valves per node.
  void _seedDemo() {
    nodes = [
      NodeEntry(name: 'Sugarcane east', zone: 'z1', node: 'n1', position: 1),
      NodeEntry(name: 'Sugarcane west', zone: 'z1', node: 'n2', position: 2),
      NodeEntry(name: 'Banana', zone: 'z2', node: 'n3', position: 1),
      NodeEntry(name: 'Turmeric', zone: 'z2', node: 'n4', position: 2),
    ];
    zones
      ..clear()
      ..['z1'] = ZoneInfo(key: 'z1', name: 'Zone 1 · Sugarcane')
      ..['z2'] = ZoneInfo(key: 'z2', name: 'Zone 2 · Banana + Turmeric', layout: 'left');
  }

  Future<void> resetDemo() async {
    _seedDemo();
    await save();
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString('wm2_demo_nodes', jsonEncode(nodes.map((n) => n.toJson()).toList()));
    await p.setString('wm2_demo_zones', jsonEncode(zones.values.map((z) => z.toJson()).toList()));
    notifyListeners();
  }
}

/// Owns the active link to one node: tries the direct link first (works with
/// no internet), then MQTT. Keeps the latest state and a short event list.
class NodeController extends ChangeNotifier {
  NodeController(this.entry, this.store);

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

  bool get connected => link?.isConnected ?? false;

  Future<void> connect() async {
    // Demo: every node is a simulated one, keyed by its zone/node name.
    _drop();
    final l = Wm2DemoLink('${entry.zone}/${entry.node}');
    await l.connect();
    _use(l);
    connecting = false;
    final r = await l.send({'cmd': 'get_state'});
    if (r['ok'] == true && r['data'] is Map) _onState(Map<String, dynamic>.from(r['data']));
  }

  void _use(NodeLink l) {
    link = l;
    status = 'connected (${l.kind})';
    _subs.add(l.state.listen(_onState));
    _subs.add(l.events.listen((e) {
      events.insert(0, e);
      if (events.length > 50) events.removeLast();
      _notify();
    }));
    _subs.add(l.connected.listen((c) {
      status = c ? 'connected (${l.kind})' : 'reconnecting (${l.kind})...';
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
    _drop();
    super.dispose();
  }
}
