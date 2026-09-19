import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Local = phone is on the Master's own WiFi (WPC-Master-XXXXXXXX), talks HTTP.
/// Cloud = phone has internet; talks to the Master through the MQTT broker.
enum LinkMode { local, cloud }

class MasterRef {
  final String id; // 8 hex chars, upper case -- same as in the SoftAP name / topics
  final String name;
  const MasterRef(this.id, this.name);

  Map<String, dynamic> toJson() => {'id': id, 'name': name};
  factory MasterRef.fromJson(Map<String, dynamic> j) =>
      MasterRef(j['id'] as String, (j['name'] as String?) ?? '');

  String get label => name.isNotEmpty ? name : 'Master $id';
}

/// Holds the connection choice (local/cloud), the list of installations
/// (one user can run several WPCs at different farms) and the MQTT link.
///
/// Anyone using the app can control any pump -- there is deliberately no
/// per-user lock. Whoever is at the farm just flips it back.
class Backend extends ChangeNotifier {
  static final Backend instance = Backend._();
  Backend._();

  // Same shared broker and per-product credential the other AgriSense
  // products use; hardening (per-user credentials / TLS) is a later step.
  static const String _broker = 'mqtt.agrisenseandcontrol.in';
  static const int _port = 1883;
  static const String _user = 'wpc-device';
  static const String _pass = 'asacwpc';

  static const _kMode = 'link_mode';
  static const _kMasters = 'masters';
  static const _kActive = 'active_master';

  LinkMode mode = LinkMode.local;
  List<MasterRef> masters = [];
  String? activeId;

  // ---- cloud state (only meaningful in cloud mode) ----
  MqttServerClient? _client;
  bool brokerConnected = false;
  String? cloudError;
  bool? masterOnline; // from the Master's last-will topic; null = unknown yet
  Map<String, dynamic>? _status;
  DateTime? _statusAt;
  int _statusCount = 0;
  bool _connecting = false;
  String? _subId; // which Master's topics the current MQTT session is subscribed to

  MasterRef? get active {
    for (final m in masters) {
      if (m.id == activeId) return m;
    }
    return null;
  }

  String _topic(String id, String suffix) => 'agrisense/WPC/WPC_$id/$suffix';

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    mode = (p.getString(_kMode) == 'cloud') ? LinkMode.cloud : LinkMode.local;
    activeId = p.getString(_kActive);
    final raw = p.getString(_kMasters);
    if (raw != null) {
      try {
        masters = (jsonDecode(raw) as List)
            .map((e) => MasterRef.fromJson(e as Map<String, dynamic>))
            .toList();
      } catch (_) {
        masters = [];
      }
    }
    if (activeId != null && active == null) activeId = null;
    notifyListeners();
  }

  Future<void> _save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(_kMode, mode == LinkMode.cloud ? 'cloud' : 'local');
    await p.setString(_kMasters, jsonEncode(masters.map((m) => m.toJson()).toList()));
    if (activeId != null) {
      await p.setString(_kActive, activeId!);
    } else {
      await p.remove(_kActive);
    }
  }

  Future<void> setMode(LinkMode m) async {
    mode = m;
    await _save();
    notifyListeners();
  }

  Future<void> addMaster(String id, String name) async {
    id = id.trim().toUpperCase();
    masters = [...masters.where((m) => m.id != id), MasterRef(id, name.trim())];
    activeId ??= id;
    await _save();
    notifyListeners();
  }

  Future<void> removeMaster(String id) async {
    masters = masters.where((m) => m.id != id).toList();
    if (activeId == id) {
      activeId = masters.isNotEmpty ? masters.first.id : null;
      _resetCloudCache();
    }
    await _save();
    notifyListeners();
  }

  Future<void> setActive(String id) async {
    if (activeId == id) return;
    activeId = id;
    _resetCloudCache();
    await _save();
    notifyListeners();
  }

  void _resetCloudCache() {
    _status = null;
    _statusAt = null;
    masterOnline = null;
  }

  // ------------------------------------------------------------------ cloud
  /// Makes the live MQTT session's subscriptions match the active Master
  /// (after a switch, or on a fresh connection).
  void _syncSubscriptions() {
    final c = _client;
    final id = activeId;
    if (c == null || id == null || _subId == id) return;
    if (_subId != null) {
      c.unsubscribe(_topic(_subId!, 'status'));
      c.unsubscribe(_topic(_subId!, 'lwt'));
    }
    c.subscribe(_topic(id, 'status'), MqttQos.atMostOnce);
    c.subscribe(_topic(id, 'lwt'), MqttQos.atMostOnce);
    _subId = id;
  }

  /// Connects (if needed) and subscribes to the active Master's topics.
  Future<void> ensureCloud() async {
    final id = activeId;
    if (id == null) {
      cloudError = 'No Master selected';
      return;
    }
    if (_client?.connectionStatus?.state == MqttConnectionState.connected) {
      _syncSubscriptions();
      return;
    }
    if (_connecting) return;
    _connecting = true;
    try {
      try {
        _client?.disconnect();
      } catch (_) {}
      final clientId = 'wpc_app_${DateTime.now().millisecondsSinceEpoch}';
      final c = MqttServerClient.withPort(_broker, clientId, _port);
      c.logging(on: false);
      c.keepAlivePeriod = 30;
      c.connectTimeoutPeriod = 8000;
      c.autoReconnect = true;
      c.resubscribeOnAutoReconnect = true;
      c.onDisconnected = () {
        brokerConnected = false;
        notifyListeners();
      };
      c.onAutoReconnected = () {
        brokerConnected = true;
        notifyListeners();
      };
      c.connectionMessage = MqttConnectMessage()
          .withClientIdentifier(clientId)
          .authenticateAs(_user, _pass)
          .startClean();
      _client = c;
      _subId = null;
      await c.connect();
      if (c.connectionStatus?.state != MqttConnectionState.connected) {
        cloudError = 'Broker refused the connection (${c.connectionStatus?.returnCode})';
        brokerConnected = false;
        return;
      }
      brokerConnected = true;
      cloudError = null;
      c.updates!.listen(_onMessages);
      _syncSubscriptions();
    } catch (e) {
      brokerConnected = false;
      cloudError = 'Cannot reach the cloud: $e';
    } finally {
      _connecting = false;
      notifyListeners();
    }
  }

  void _onMessages(List<MqttReceivedMessage<MqttMessage?>> events) {
    for (final e in events) {
      final msg = e.payload as MqttPublishMessage;
      final text = MqttPublishPayload.bytesToStringAsString(msg.payload.message);
      final t = e.topic;
      if (activeId == null || !t.startsWith('agrisense/WPC/WPC_$activeId/')) continue;
      try {
        final j = jsonDecode(text) as Map<String, dynamic>;
        if (t.endsWith('/status')) {
          _status = j;
          _statusAt = DateTime.now();
          _statusCount++;
        } else if (t.endsWith('/lwt')) {
          masterOnline = j['online'] == true;
        }
      } catch (_) {}
    }
    notifyListeners();
  }

  /// Latest status from the Master. Adds two fields the screens use:
  /// `_masterOnline` (false when the broker says the Master's link is down --
  /// the retained status can be hours old then, so it must not look live)
  /// and `_ageSec`.
  Future<Map<String, dynamic>> cloudStatus() async {
    await ensureCloud();
    if (!brokerConnected) throw Exception(cloudError ?? 'Not connected to the cloud');
    final end = DateTime.now().add(const Duration(seconds: 8));
    while (_status == null && DateTime.now().isBefore(end)) {
      await Future.delayed(const Duration(milliseconds: 200));
    }
    final s = _status;
    if (s == null) {
      throw Exception('No status from this Master yet -- is it powered and on the farm WiFi?');
    }
    final out = Map<String, dynamic>.from(s);
    out['_masterOnline'] = masterOnline != false;
    out['_ageSec'] = DateTime.now().difference(_statusAt!).inSeconds;
    return out;
  }

  /// Publishes a command to the Master. Never retained, so a command can't
  /// replay when the Master reconnects. Waits briefly for the refreshed
  /// status the Master publishes after acting on it.
  Future<void> sendCloudCommand(Map<String, dynamic> cmd) async {
    await ensureCloud();
    final id = activeId;
    final c = _client;
    if (id == null || c == null || !brokerConnected) {
      throw Exception(cloudError ?? 'Not connected to the cloud');
    }
    if (masterOnline == false) {
      throw Exception('This Master is offline -- command not sent');
    }
    final before = _statusCount;
    final b = MqttClientPayloadBuilder()..addString(jsonEncode(cmd));
    c.publishMessage(_topic(id, 'command'), MqttQos.atLeastOnce, b.payload!, retain: false);
    final end = DateTime.now().add(const Duration(seconds: 4));
    while (_statusCount == before && DateTime.now().isBefore(end)) {
      await Future.delayed(const Duration(milliseconds: 150));
    }
  }
}
