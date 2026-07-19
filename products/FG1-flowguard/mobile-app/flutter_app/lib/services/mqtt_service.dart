import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';
import 'device_service.dart';
import '../models/device_status.dart';
import '../models/history_entry.dart';
import '../models/cycle.dart';

class MqttService implements DeviceService {
  // Was hardcoded to the AlgoMomentum VPS IP (87.76.191.157) — a
  // genuinely different, auth-required broker from what firmware
  // actually defaults to. That mismatch meant the app and device could
  // never rendezvous over Cloud mode even when both had real internet.
  static const String _broker = 'mqtt.grty.co.in';
  static const int    _port   = 1883;

  // Topics are now scoped to a specific physical device via its 4-char
  // MAC suffix (entered by the user in Settings — see
  // deviceSuffixProvider) — previously a fixed 'SWC_001' shared by every
  // device, so with more than one device in Cloud mode, a command meant
  // for one would reach all of them, and their retained status topics
  // would overwrite each other.
  final String deviceSuffix;
  late final String _topicStatus, _topicCmd, _topicHistory, _topicCycles;

  MqttService({required this.deviceSuffix}) {
    final base = 'swc/SWC_001/$deviceSuffix';
    _topicStatus  = '$base/status';
    _topicCmd     = '$base/command';
    _topicHistory = '$base/history';
    _topicCycles  = '$base/cycles';
  }

  MqttServerClient? _client;
  bool _isConnecting = false;

  final _statusController    = StreamController<DeviceStatus>.broadcast();
  final _historyController   = StreamController<List<HistoryEntry>>.broadcast();
  final _cyclesController    = StreamController<List<Cycle>>.broadcast();
  final _connectedController = StreamController<bool>.broadcast();

  Stream<DeviceStatus>       get statusStream    => _statusController.stream;
  Stream<List<HistoryEntry>> get historyStream   => _historyController.stream;
  Stream<List<Cycle>>        get cyclesStream    => _cyclesController.stream;
  Stream<bool>               get connectedStream => _connectedController.stream;

  bool get isConnected =>
      _client?.connectionStatus?.state == MqttConnectionState.connected;

  Future<bool> connect() async {
    if (_isConnecting) return false;
    _isConnecting = true;

    try { _client?.disconnect(); } catch (_) {}
    _client = null;

    final clientId = 'swc_app_${DateTime.now().millisecondsSinceEpoch}';
    _client = MqttServerClient.withPort(_broker, clientId, _port);
    _client!.logging(on: false);
    _client!.keepAlivePeriod = 30;
    _client!.connectTimeoutPeriod = 10000;
    _client!.onDisconnected = _onDisconnected;
    _client!.onConnected    = _onConnected;

    final connMsg = MqttConnectMessage()
        .withClientIdentifier(clientId)
        .startClean();
    _client!.connectionMessage = connMsg;

    try {
      await _client!.connect();
    } on SocketException catch (e) {
      print('[MQTT] Socket error: $e');
      _isConnecting = false;
      _connectedController.add(false);
      return false;
    } on NoConnectionException catch (e) {
      print('[MQTT] No connection: $e');
      _isConnecting = false;
      _connectedController.add(false);
      return false;
    } catch (e) {
      print('[MQTT] Connect error: $e');
      _isConnecting = false;
      _connectedController.add(false);
      return false;
    }

    if (_client!.connectionStatus!.state != MqttConnectionState.connected) {
      print('[MQTT] Not connected: ${_client!.connectionStatus!.returnCode}');
      _isConnecting = false;
      _connectedController.add(false);
      return false;
    }

    print('[MQTT] Connected — subscribing');
    _client!.subscribe(_topicStatus,  MqttQos.atMostOnce);
    _client!.subscribe(_topicHistory, MqttQos.atMostOnce);
    _client!.subscribe(_topicCycles,  MqttQos.atMostOnce);

    _client!.updates?.listen(
      (List<MqttReceivedMessage<MqttMessage>> msgs) {
        for (final msg in msgs) {
          try {
            final pub     = msg.payload as MqttPublishMessage;
            final payload = MqttPublishPayload.bytesToStringAsString(
                                pub.payload.message);
            print('[MQTT] Received on ${msg.topic}: $payload');
            _handleMessage(msg.topic, payload);
          } catch (e) {
            print('[MQTT] Message error: $e');
          }
        }
      },
      onError: (e) => print('[MQTT] Stream error: $e'),
      cancelOnError: false,
    );

    _isConnecting = false;
    _connectedController.add(true);
    return true;
  }

  void _handleMessage(String topic, String payload) {
    try {
      if (topic == _topicStatus) {
        final json = jsonDecode(payload) as Map<String, dynamic>;
        _statusController.add(DeviceStatus.fromJson(json));
      } else if (topic == _topicHistory) {
        final list = (jsonDecode(payload) as List)
            .map((e) => HistoryEntry.fromJson(e as Map<String, dynamic>))
            .toList();
        _historyController.add(list);
      } else if (topic == _topicCycles) {
        final list = (jsonDecode(payload) as List)
            .map((e) => Cycle.fromJson(e as Map<String, dynamic>))
            .toList();
        _cyclesController.add(list);
      }
    } catch (e) {
      print('[MQTT] Parse error on $topic: $e');
    }
  }

  void _publish(Map<String, dynamic> payload) {
    if (!isConnected) { print('[MQTT] Not connected'); return; }
    try {
      final builder = MqttClientPayloadBuilder();
      builder.addString(jsonEncode(payload));
      _client!.publishMessage(_topicCmd, MqttQos.atMostOnce, builder.payload!);
    } catch (e) {
      print('[MQTT] Publish error: $e');
    }
  }

  void manualOn()                  => _publish({'cmd': 'manual_on'});
  @override
  void sendRaw(Map<String, dynamic> payload) => _publish(payload);
  void manualOff()                 => _publish({'cmd': 'manual_off'});
  void manualLiters(double liters) => _publish({'cmd': 'manual_liters', 'liters': liters});
  void stopCycle()                 => _publish({'cmd': 'stop_cycle'});
  void pauseCycle()                => _publish({'cmd': 'pause_cycle'});
  void resumeCycle()               => _publish({'cmd': 'resume_cycle'});
  void getHistory()                => _publish({'cmd': 'get_history'});

  // Firmware stores RTC timestamps as IST wall-clock fields treated as UTC
  // (see RTCManager::syncFromTm) — so range queries must encode from/to the
  // same way: take the wall-clock date/time fields and reinterpret as UTC,
  // rather than converting through the phone's real timezone offset.
  int _deviceEpoch(DateTime wallClock) => DateTime.utc(
        wallClock.year, wallClock.month, wallClock.day,
        wallClock.hour, wallClock.minute, wallClock.second,
      ).millisecondsSinceEpoch ~/ 1000;

  void getHistoryRange(DateTime from, DateTime to) => _publish({
        'cmd': 'get_history_range',
        'from': _deviceEpoch(from),
        'to': _deviceEpoch(to),
      });
  void getCycles()                 => _publish({'cmd': 'get_cycles'});
  void setCycles(List<Cycle> cycles) => _publish({
        'cmd': 'set_cycles',
        'cycles': cycles.map((c) => c.toJson()).toList(),
      });

  void _onConnected()    => _connectedController.add(true);
  void _onDisconnected() {
    print('[MQTT] Disconnected — retrying in 5s');
    _connectedController.add(false);
    Future.delayed(const Duration(seconds: 5), () {
      if (!isConnected) connect();
    });
  }

  void dispose() {
    _statusController.close();
    _historyController.close();
    _cyclesController.close();
    _connectedController.close();
    try { _client?.disconnect(); } catch (_) {}
  }
}
