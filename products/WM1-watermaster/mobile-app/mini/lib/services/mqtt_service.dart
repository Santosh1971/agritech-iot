import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';
import 'device_service.dart';
import 'network_binding.dart';
import '../models/device_status.dart';
import '../models/program.dart';
import '../models/history_record.dart';

/// Cloud transport over the shared AgriSense broker — same broker FG1
/// already uses. Topics mirror FG1's mqtt_service.dart pattern (see
/// firmware/mini/WM1Firmware/src/MqttClientWrapper.h for the device side):
///   agrisense/WM1/<deviceId>/status            (retained)
///   agrisense/WM1/<deviceId>/programs
///   agrisense/WM1/<deviceId>/command           (this app -> device)
///   agrisense/WM1/<deviceId>/programs_config   (this app -> device, RETAINED)
///   agrisense/WM1/<deviceId>/lwt               (retained, {"online":bool})
///   agrisense/WM1/<deviceId>/history            (get_history's reply — no periodic
///                                                 broadcast exists for this one)
class MqttService implements DeviceService {
  static const String _broker = 'mqtt.agrisenseandcontrol.in';
  // Per-product credential, same simplification FG1 currently uses
  // (not per-physical-device ACLs) — see Config.h's TODO on this.
  static const String _mqttUser = 'wm1-device';
  static const String _mqttPass = 'asacwm1';
  static const int _port = 1883;

  final String deviceSuffix; // last 8 hex chars, e.g. "1A2B3FDE" -> WM1_1A2B3FDE
  late final String _topicStatus, _topicPrograms, _topicLibrary, _topicCommand,
      _topicProgramsConfig, _topicLibraryConfig, _topicLwt, _topicHistory;

  MqttService({required this.deviceSuffix}) {
    final base = 'agrisense/WM1/WM1_$deviceSuffix';
    _topicStatus = '$base/status';
    _topicPrograms = '$base/programs';
    _topicLibrary = '$base/library';
    _topicCommand = '$base/command';
    _topicProgramsConfig = '$base/programs_config';
    _topicLibraryConfig = '$base/library_config';
    _topicLwt = '$base/lwt';
    _topicHistory = '$base/history';
  }

  MqttServerClient? _client;
  bool _isConnecting = false;

  final _statusController = StreamController<DeviceStatus>.broadcast();
  final _programsController = StreamController<List<Program>>.broadcast();
  final _libraryController = StreamController<List<LibrarySequence>>.broadcast();
  final _connectedController = StreamController<bool>.broadcast();
  final _deviceOnlineController = StreamController<bool>.broadcast();
  final _historyController = StreamController<List<HistoryRecord>>.broadcast();

  @override
  Stream<DeviceStatus> get statusStream => _statusController.stream;
  @override
  Stream<List<Program>> get programsStream => _programsController.stream;
  @override
  Stream<List<LibrarySequence>> get libraryStream => _libraryController.stream;
  @override
  Stream<bool> get connectedStream => _connectedController.stream;
  @override
  Stream<bool> get deviceOnlineStream => _deviceOnlineController.stream;
  @override
  Stream<List<HistoryRecord>> get historyStream => _historyController.stream;

  @override
  bool get isConnected => _client?.connectionStatus?.state == MqttConnectionState.connected;

  @override
  Future<bool> connect() async {
    if (_isConnecting) return false;
    _isConnecting = true;

    // Undo Local mode's WiFi-only network binding, if any — Cloud mode
    // needs a network that actually has internet, and staying bound to
    // a no-internet SoftAP would strand this connection attempt exactly
    // the way Local mode was stranding WebSocket attempts before that
    // binding existed.
    await NetworkBinding.unbind();

    try { _client?.disconnect(); } catch (_) {}
    _client = null;

    final clientId = 'wm1_app_${DateTime.now().millisecondsSinceEpoch}';
    _client = MqttServerClient.withPort(_broker, clientId, _port);
    _client!.logging(on: false);
    _client!.keepAlivePeriod = 30;
    _client!.connectTimeoutPeriod = 10000;
    _client!.onDisconnected = _onDisconnected;
    _client!.onConnected = _onConnected;

    final connMsg = MqttConnectMessage()
        .withClientIdentifier(clientId)
        .authenticateAs(_mqttUser, _mqttPass)
        .startClean();
    _client!.connectionMessage = connMsg;

    try {
      await _client!.connect();
    } on SocketException catch (e) {
      print('[MQTT] Socket error: $e');
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
    _client!.subscribe(_topicStatus, MqttQos.atMostOnce);
    _client!.subscribe(_topicPrograms, MqttQos.atMostOnce);
    _client!.subscribe(_topicLibrary, MqttQos.atMostOnce);
    _client!.subscribe(_topicLwt, MqttQos.atMostOnce);
    _client!.subscribe(_topicHistory, MqttQos.atMostOnce);

    _client!.updates?.listen((List<MqttReceivedMessage<MqttMessage>> msgs) {
      for (final msg in msgs) {
        try {
          final pub = msg.payload as MqttPublishMessage;
          final payload = MqttPublishPayload.bytesToStringAsString(pub.payload.message);
          _handleMessage(msg.topic, payload);
        } catch (e) {
          print('[MQTT] Message error: $e');
        }
      }
    }, onError: (e) => print('[MQTT] Stream error: $e'), cancelOnError: false);

    _isConnecting = false;
    _connectedController.add(true);
    // Bug fix: programs/library only ever populated if whichever screen
    // needs them happened to be freshly (re)mounted after connecting —
    // status arrives fine on its own since that topic is retained, but
    // programs/library are NOT retained, so a screen that was already
    // open before this connection (e.g. across a Local->Cloud mode
    // switch) would just keep showing nothing, easily misread as
    // "my schedule got deleted" when it was actually never re-fetched.
    // Requesting both immediately on every successful connect means the
    // streams have real data regardless of what's mounted when.
    getPrograms();
    getLibrary();
    return true;
  }

  void _handleMessage(String topic, String payload) {
    try {
      if (topic == _topicStatus) {
        _statusController.add(DeviceStatus.fromJson(jsonDecode(payload) as Map<String, dynamic>));
      } else if (topic == _topicPrograms) {
        final list = (jsonDecode(payload) as List)
            .map((e) => Program.fromJson(e as Map<String, dynamic>))
            .toList();
        _programsController.add(list);
      } else if (topic == _topicLibrary) {
        final list = (jsonDecode(payload) as List)
            .map((e) => LibrarySequence.fromJson(e as Map<String, dynamic>))
            .toList();
        _libraryController.add(list);
      } else if (topic == _topicLwt) {
        final json = jsonDecode(payload) as Map<String, dynamic>;
        _deviceOnlineController.add(json['online'] == true);
      } else if (topic == _topicHistory) {
        // Firmware publishes get_history's own {"ok","cmd","data"} reply
        // shape here (see MqttClientWrapper.h), not a bare array, so this
        // unwraps it the same way a direct command reply would be.
        final wrapped = jsonDecode(payload) as Map<String, dynamic>;
        if (wrapped['data'] is List) {
          final list = (wrapped['data'] as List)
              .map((e) => HistoryRecord.fromJson(e as Map<String, dynamic>))
              .toList();
          _historyController.add(list);
        }
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
      _client!.publishMessage(_topicCommand, MqttQos.atMostOnce, builder.payload!);
    } catch (e) {
      print('[MQTT] Publish error: $e');
    }
  }

  void _publishRetained(String topic, Map<String, dynamic> payload) {
    if (!isConnected) { print('[MQTT] Not connected'); return; }
    try {
      final builder = MqttClientPayloadBuilder();
      builder.addString(jsonEncode(payload));
      _client!.publishMessage(topic, MqttQos.atLeastOnce, builder.payload!, retain: true);
    } catch (e) {
      print('[MQTT] Publish error: $e');
    }
  }

  @override
  void sendRaw(Map<String, dynamic> payload) => _publish(payload);
  @override
  void manualSet(String channel, bool state) =>
      _publish({'cmd': 'manual_set', 'channel': channel, 'state': state});
  @override
  void triggerProgram(int programId, int seqIndex) =>
      _publish({'cmd': 'trigger_program', 'id': programId, 'seq': seqIndex});
  @override
  void forceStop() => _publish({'cmd': 'force_stop'});
  @override
  void pause() => _publish({'cmd': 'pause'});
  @override
  void resume() => _publish({'cmd': 'resume'});
  @override
  void getPrograms() => _publish({'cmd': 'list_programs'});
  @override
  void setPrograms(List<Program> programs) => _publishRetained(_topicProgramsConfig, {
        'cmd': 'set_programs',
        'programs': programs.map((p) => p.toJson()).toList(),
      });
  @override
  void getLibrary() => _publish({'cmd': 'list_sequence_library'});
  @override
  void setLibrary(List<LibrarySequence> library) => _publishRetained(_topicLibraryConfig, {
        'cmd': 'set_sequence_library',
        'sequences': library.map((e) => e.toJson()).toList(),
      });
  @override
  void simulatePowerLoss() => _publish({'cmd': 'simulate_power_loss'});
  @override
  void simulatePowerRestore() => _publish({'cmd': 'simulate_power_restore'});
  @override
  void getHistory({int since = 0, int max = 500}) => _publish({'cmd': 'get_history', 'since': since, 'max': max});

  void _onConnected() => _connectedController.add(true);
  void _onDisconnected() {
    print('[MQTT] Disconnected — retrying in 5s');
    _connectedController.add(false);
    _deviceOnlineController.add(false);
    Future.delayed(const Duration(seconds: 5), () {
      if (!isConnected) connect();
    });
  }

  @override
  void dispose() {
    _statusController.close();
    _programsController.close();
    _libraryController.close();
    _connectedController.close();
    _deviceOnlineController.close();
    _historyController.close();
    try { _client?.disconnect(); } catch (_) {}
  }
}
