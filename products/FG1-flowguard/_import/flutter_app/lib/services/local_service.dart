import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'device_service.dart';
import '../models/device_status.dart';
import '../models/history_entry.dart';
import '../models/cycle.dart';

/// Talks directly to the device over its own SoftAP — no broker involved.
/// Assumes the phone is already connected to the device's WiFi network
/// (SSID like SWC_001_XXXX), where the ESP32's gateway IP is always
/// 192.168.4.1 — so no discovery/mDNS is needed for this mode.
///
/// Uses one WebSocket connection for both directions: incoming
/// {"type": "...", "data": ...} broadcasts feed the streams (mirrors
/// MQTT's status/history/cycles topics), and outgoing commands use the
/// same {"cmd": "...", ...} schema MQTT/BLE always used — LocalServer on
/// the firmware side replies with {"ok":..., "cmd":..., "data":...}
/// directly to the requesting connection.
class LocalService implements DeviceService {
  static const String defaultHost = '192.168.4.1';

  String _host;
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connected = false;

  final _statusController    = StreamController<DeviceStatus>.broadcast();
  final _historyController   = StreamController<List<HistoryEntry>>.broadcast();
  final _cyclesController    = StreamController<List<Cycle>>.broadcast();
  final _connectedController = StreamController<bool>.broadcast();
  // Raw command responses ({"ok":..,"cmd":..,"data":..}) — used by
  // provisioning screens that need the actual response payload
  // (e.g. device_info, wifi_scan), not just a fire-and-forget command.
  final _responseController  = StreamController<Map<String, dynamic>>.broadcast();

  LocalService({String host = defaultHost}) : _host = host;

  @override
  Stream<DeviceStatus>       get statusStream    => _statusController.stream;
  @override
  Stream<List<HistoryEntry>> get historyStream   => _historyController.stream;
  @override
  Stream<List<Cycle>>        get cyclesStream    => _cyclesController.stream;
  @override
  Stream<bool>                get connectedStream => _connectedController.stream;
  Stream<Map<String, dynamic>> get responseStream  => _responseController.stream;

  @override
  bool get isConnected => _connected;

  /// Change the target host (e.g. if a user manually enters an IP instead
  /// of relying on the SoftAP default).
  void setHost(String host) {
    _host = host;
  }

  @override
  Future<bool> connect() async {
    if (_connected && _channel != null) return true; // already connected — don't tear down a working socket

    try { await _channel?.sink.close(); } catch (_) {}
    _sub?.cancel();

    try {
      _channel = WebSocketChannel.connect(Uri.parse('ws://$_host/ws'));
      _sub = _channel!.stream.listen(
        _handleMessage,
        onDone: () {
          _connected = false;
          _connectedController.add(false);
        },
        onError: (e) {
          print('[Local] WS error: $e');
          _connected = false;
          _connectedController.add(false);
        },
        cancelOnError: false,
      );
      // WebSocketChannel.connect() doesn't await the handshake — give it
      // a brief moment before declaring success.
      await Future.delayed(const Duration(milliseconds: 300));
      _connected = true;
      _connectedController.add(true);
      return true;
    } catch (e) {
      print('[Local] Connect error: $e');
      _connected = false;
      _connectedController.add(false);
      return false;
    }
  }

  void _handleMessage(dynamic raw) {
    try {
      final msg = jsonDecode(raw as String) as Map<String, dynamic>;

      // Broadcast-style push: {"type": "status"|"cycles"|"history"|"active_cycle", "data": ...}
      if (msg.containsKey('type')) {
        final type = msg['type'];
        final data = msg['data'];
        if (type == 'status' && data is Map<String, dynamic>) {
          _statusController.add(DeviceStatus.fromJson(data));
        } else if (type == 'cycles' && data is List) {
          _cyclesController.add(
              data.map((e) => Cycle.fromJson(e as Map<String, dynamic>)).toList());
        } else if (type == 'history' && data is List) {
          _historyController.add(
              data.map((e) => HistoryEntry.fromJson(e as Map<String, dynamic>)).toList());
        }
        return;
      }

      // Command response: {"ok": bool, "cmd": "...", "data": ...}
      if (msg.containsKey('ok')) {
        _responseController.add(msg);
      }
    } catch (e) {
      print('[Local] Parse error: $e');
    }
  }

  void _send(Map<String, dynamic> payload) {
    if (_channel == null) { print('[Local] Not connected'); return; }
    try {
      _channel!.sink.add(jsonEncode(payload));
    } catch (e) {
      print('[Local] Send error: $e');
    }
  }

  /// Escape hatch for provisioning commands not part of the common
  /// DeviceService interface (wifi_config, mqtt_config, rtc_sync,
  /// calibrate, relay_test, factory_reset, wifi_scan, device_info).
  /// Same {"cmd": ...} schema as everything else.
  void sendRaw(Map<String, dynamic> payload) => _send(payload);

  @override
  void manualOn()                  => _send({'cmd': 'manual_on'});
  @override
  void manualOff()                 => _send({'cmd': 'manual_off'});
  @override
  void manualLiters(double liters) => _send({'cmd': 'manual_liters', 'liters': liters});
  @override
  void stopCycle()                 => _send({'cmd': 'stop_cycle'});
  @override
  void pauseCycle()                => _send({'cmd': 'pause_cycle'});
  @override
  void resumeCycle()                => _send({'cmd': 'resume_cycle'});
  @override
  void getHistory()                => _send({'cmd': 'get_history'});

  // Same wall-clock-as-UTC encoding as MqttService — see its comment.
  int _deviceEpoch(DateTime wallClock) => DateTime.utc(
        wallClock.year, wallClock.month, wallClock.day,
        wallClock.hour, wallClock.minute, wallClock.second,
      ).millisecondsSinceEpoch ~/ 1000;

  @override
  void getHistoryRange(DateTime from, DateTime to) => _send({
        'cmd': 'get_history_range',
        'from': _deviceEpoch(from),
        'to': _deviceEpoch(to),
      });
  @override
  void getCycles() => _send({'cmd': 'get_cycles'});
  @override
  void setCycles(List<Cycle> cycles) => _send({
        'cmd': 'set_cycles',
        'cycles': cycles.map((c) => c.toJson()).toList(),
      });

  @override
  void dispose() {
    _statusController.close();
    _historyController.close();
    _cyclesController.close();
    _connectedController.close();
    _responseController.close();
    _sub?.cancel();
    try { _channel?.sink.close(); } catch (_) {}
  }
}
