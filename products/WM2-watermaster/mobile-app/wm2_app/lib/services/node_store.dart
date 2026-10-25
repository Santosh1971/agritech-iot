import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'local_link.dart';
import 'mqtt_link.dart';
import 'node_link.dart';

/// A node the user added: reachable directly (host) and/or through MQTT on
/// the AgriSense broker (agrisense/WM2/DEVICE_ID/) or Girish broker
/// (FARM/wm2/ZONE/NODE/).
class NodeEntry {
  String name;
  String host; // IP, or 192.168.4.1 on SoftAP; empty = MQTT only
  String broker; // 'agrisense' | 'girish'
  String agId; // WM2_A98803E8 (filled in automatically on first local connect)
  String farm, zone, node;
  NodeEntry(
      {required this.name,
      this.host = '',
      this.broker = 'agrisense',
      this.agId = '',
      this.farm = 'girish',
      this.zone = 'z1',
      this.node = 'n1'});

  String get base => broker == 'girish' ? '$farm/wm2/$zone/$node/' : 'agrisense/WM2/$agId/';
  Map<String, dynamic> toJson() =>
      {'name': name, 'host': host, 'broker': broker, 'agId': agId, 'farm': farm, 'zone': zone, 'node': node};
  factory NodeEntry.fromJson(Map<String, dynamic> j) => NodeEntry(
      name: j['name'] ?? 'Node',
      host: j['host'] ?? '',
      broker: j['broker'] ?? 'agrisense',
      agId: j['agId'] ?? '',
      farm: j['farm'] ?? 'girish',
      zone: j['zone'] ?? 'z1',
      node: j['node'] ?? 'n1');
}

class NodeStore extends ChangeNotifier {
  List<NodeEntry> nodes = [];
  BrokerSettings agrisense = BrokerSettings(host: 'mqtt.agrisenseandcontrol.in');
  BrokerSettings girish = BrokerSettings();

  BrokerSettings brokerFor(NodeEntry e) => e.broker == 'girish' ? girish : agrisense;

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    final raw = p.getString('nodes');
    if (raw != null) {
      nodes = (jsonDecode(raw) as List).map((e) => NodeEntry.fromJson(Map<String, dynamic>.from(e))).toList();
    }
    final a = p.getString('broker_agrisense');
    if (a != null) agrisense = BrokerSettings.fromJson(Map<String, dynamic>.from(jsonDecode(a)));
    final g = p.getString('broker_girish');
    if (g != null) girish = BrokerSettings.fromJson(Map<String, dynamic>.from(jsonDecode(g)));
    notifyListeners();
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString('nodes', jsonEncode(nodes.map((n) => n.toJson()).toList()));
    await p.setString('broker_agrisense', jsonEncode(agrisense.toJson()));
    await p.setString('broker_girish', jsonEncode(girish.toJson()));
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
    if (connecting) return;
    connecting = true;
    status = 'connecting...';
    _notify();
    _drop();
    if (entry.host.trim().isNotEmpty) {
      final l = LocalLink(entry.host.trim());
      if (await l.connect()) {
        _use(l);
        connecting = false;
        return;
      }
      l.dispose();
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
            : 'node not reachable';
    _notify();
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
