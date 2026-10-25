import 'dart:async';
import 'dart:convert';

import 'package:mqtt_client/mqtt_client.dart';

import 'mqtt_factory_io.dart' if (dart.library.html) 'mqtt_factory_web.dart';
import 'node_link.dart';

class BrokerSettings {
  String host;
  int port;
  String user;
  String pass;
  BrokerSettings({this.host = '', this.port = 1883, this.user = '', this.pass = ''});

  bool get isSet => host.trim().isNotEmpty;

  Map<String, dynamic> toJson() => {'host': host, 'port': port, 'user': user, 'pass': pass};
  factory BrokerSettings.fromJson(Map<String, dynamic> j) => BrokerSettings(
      host: j['host'] ?? '', port: j['port'] ?? 1883, user: j['user'] ?? '', pass: j['pass'] ?? '');
}

/// Link through Girish's MQTT broker: FARM/wm2/ZONE/NODE/ + cmd, resp, state, event, status, telemetry.
class MqttLink with ReplyMatcher implements NodeLink {
  MqttLink(this.broker, this.base);

  final BrokerSettings broker;
  final String base; // e.g. girish/wm2/z1/n1/
  MqttClient? _c;
  StreamSubscription? _sub;
  bool _connected = false;
  bool _nodeOnline = false;

  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final _conn = StreamController<bool>.broadcast();

  bool get nodeOnline => _nodeOnline;

  @override
  String get kind => 'MQTT';
  @override
  Stream<Map<String, dynamic>> get state => _state.stream;
  @override
  Stream<Map<String, dynamic>> get events => _events.stream;
  @override
  Stream<bool> get connected => _conn.stream;
  @override
  bool get isConnected => _connected;

  @override
  Future<bool> connect() async {
    if (_connected) return true;
    final c = makeMqttClient(broker.host, broker.port, 'wm2app-${DateTime.now().millisecondsSinceEpoch % 1000000}');
    c.keepAlivePeriod = 30;
    c.logging(on: false);
    c.autoReconnect = true;
    c.onDisconnected = () => _set(false);
    c.onConnected = () => _set(true);
    c.onAutoReconnected = () {
      _set(true);
      _subscribe();
    };
    try {
      await c.connect(broker.user.isEmpty ? null : broker.user, broker.pass.isEmpty ? null : broker.pass)
          .timeout(const Duration(seconds: 6));
    } catch (_) {
      _set(false);
      return false;
    }
    if (c.connectionStatus?.state != MqttConnectionState.connected) {
      _set(false);
      return false;
    }
    _c = c;
    _sub = c.updates?.listen(_onMsgs);
    _subscribe();
    _set(true);
    return true;
  }

  void _subscribe() {
    for (final t in ['state', 'event', 'resp', 'status', 'telemetry']) {
      _c?.subscribe('$base$t', MqttQos.atLeastOnce);
    }
  }

  void _onMsgs(List<MqttReceivedMessage<MqttMessage>> msgs) {
    for (final m in msgs) {
      final p = m.payload as MqttPublishMessage;
      final text = utf8.decode(p.payload.message);
      final sub = m.topic.substring(base.length);
      if (sub == 'status') {
        _nodeOnline = text == 'online';
        continue;
      }
      try {
        final j = jsonDecode(text) as Map<String, dynamic>;
        if (sub == 'state' || sub == 'telemetry') {
          if (j['backfill'] != true) _state.add(j);
        } else if (sub == 'event') {
          _events.add(j);
        } else if (sub == 'resp') {
          completeReply(j);
        }
      } catch (_) {}
    }
  }

  void _set(bool v) {
    _connected = v;
    if (!v) failAll('broker disconnected');
    if (!_conn.isClosed) _conn.add(v);
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd,
      {Duration timeout = const Duration(seconds: 15)}) async {
    if (_c == null || !_connected) return {'ok': false, 'err': 'broker not connected'};
    final id = nextId();
    final b = MqttClientPayloadBuilder()..addUTF8String(jsonEncode({...cmd, 'id': id, 'src': 'app'}));
    final f = awaitReply(id, timeout);
    _c!.publishMessage('${base}cmd', MqttQos.atLeastOnce, b.payload!);
    return f;
  }

  @override
  void dispose() {
    _sub?.cancel();
    try {
      _c?.disconnect();
    } catch (_) {}
    _state.close();
    _events.close();
    _conn.close();
  }
}
