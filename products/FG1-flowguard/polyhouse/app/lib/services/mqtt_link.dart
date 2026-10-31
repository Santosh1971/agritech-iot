import 'dart:async';
import 'dart:convert';

import 'package:mqtt_client/mqtt_client.dart';

import 'mqtt_factory_io.dart' if (dart.library.html) 'mqtt_factory_web.dart';
import 'node_link.dart';
import 'secrets.dart';

/// Link through the ASAC broker (internet). Topics, as in the controller
/// firmware (Cloud.h): agrisense/FG1/(FG1P_xxxx)/ + status | resp | lwt |
/// command | settings.
class MqttLink with ReplyMatcher implements NodeLink {
  MqttLink(this.deviceId);

  static const host = 'mqtt.agrisenseandcontrol.in';
  static const port = 1883;
  static const user = mqttUser; // from secrets.dart (git-ignored)
  static const pass = mqttPass;

  final String deviceId;
  String get base => 'agrisense/FG1/$deviceId/';

  MqttClient? _c;
  StreamSubscription? _sub;
  bool _connected = false;
  bool _disposed = false;
  bool? deviceOnline; // from the retained lwt topic
  Timer? _retry;

  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final _conn = StreamController<bool>.broadcast();

  @override
  String get kind => 'Cloud';
  @override
  Stream<Map<String, dynamic>> get state => _state.stream;
  @override
  Stream<Map<String, dynamic>> get events => _events.stream;
  @override
  Stream<bool> get connected => _conn.stream;
  @override
  bool get isConnected => _connected && deviceOnline != false;

  @override
  Future<bool> connect() async {
    if (_connected || _disposed) return _connected;
    final c = makeMqttClient(host, port, 'asacapp-${DateTime.now().millisecondsSinceEpoch % 10000000}');
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
      await c.connect(user, pass).timeout(const Duration(seconds: 8));
    } catch (_) {
      _set(false);
      _scheduleRetry();
      return false;
    }
    if (c.connectionStatus?.state != MqttConnectionState.connected) {
      _set(false);
      _scheduleRetry();
      return false;
    }
    _c = c;
    _sub = c.updates?.listen(_onMsgs);
    _subscribe();
    _set(true);
    return true;
  }

  void _scheduleRetry() {
    if (_disposed) return;
    _retry?.cancel();
    _retry = Timer(const Duration(seconds: 10), connect);
  }

  void _subscribe() {
    for (final t in ['status', 'resp', 'lwt']) {
      _c?.subscribe('$base$t', MqttQos.atMostOnce);
    }
  }

  void _onMsgs(List<MqttReceivedMessage<MqttMessage>> msgs) {
    for (final m in msgs) {
      final p = m.payload as MqttPublishMessage;
      final text = utf8.decode(p.payload.message);
      if (!m.topic.startsWith(base)) continue;
      final sub = m.topic.substring(base.length);
      try {
        final j = jsonDecode(text) as Map<String, dynamic>;
        if (sub == 'lwt') {
          deviceOnline = j['online'] == true;
          if (!_conn.isClosed) _conn.add(isConnected);
        } else if (sub == 'status') {
          _state.add(j);
        } else if (sub == 'resp') {
          if (j['error'] != null) j['err'] = j['error'];
          completeReply(j);
        }
      } catch (_) {}
    }
  }

  void _set(bool v) {
    _connected = v;
    if (!v) failAll('internet link lost');
    if (!_conn.isClosed) _conn.add(isConnected);
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd,
      {Duration timeout = const Duration(seconds: 15)}) async {
    if (_c == null || !_connected) return {'ok': false, 'err': 'not connected to the internet server'};
    final id = nextId();
    final body = jsonEncode({...cmd, 'id': id});
    final b = MqttClientPayloadBuilder()..addUTF8String(body);
    final f = awaitReply(id, timeout);
    if (cmd['cmd'] == 'set_settings') {
      // Retained: reaches the controller even if it is offline right now.
      _c!.publishMessage('${base}settings', MqttQos.atLeastOnce, b.payload!, retain: true);
    } else {
      _c!.publishMessage('${base}command', MqttQos.atLeastOnce, b.payload!);
    }
    return f;
  }

  @override
  void dispose() {
    _disposed = true;
    _retry?.cancel();
    _sub?.cancel();
    try {
      _c?.disconnect();
    } catch (_) {}
    _state.close();
    _events.close();
    _conn.close();
  }
}
