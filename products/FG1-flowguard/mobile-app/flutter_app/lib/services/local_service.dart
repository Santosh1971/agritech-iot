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

  // Generation counter: every connect attempt gets a new number and all
  // callbacks from an older channel are ignored. Without it, a superseded
  // attempt failing late (its socket was closed by the newer attempt)
  // flipped _connected back to false while the newer socket was healthy,
  // leaving the dashboard "Offline" though commands and status still
  // flowed.
  int _gen = 0;
  Future<bool>? _connecting;
  bool _wantConnected = false;
  int _backoffStep = 0;
  Timer? _reconnectTimer;
  Timer? _silenceTimer;

  static const List<int> _backoffSeconds = [2, 3, 5, 8];
  // The firmware pushes a status every 5s (STATUS_PUBLISH_INTERVAL_MS), so
  // this long without any message means the link is dead even if the
  // socket never reported closing.
  static const Duration _silenceTimeout = Duration(seconds: 15);

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
  @override
  Stream<bool>                get deviceOnlineStream => _connectedController.stream;
  Stream<Map<String, dynamic>> get responseStream  => _responseController.stream;

  @override
  bool get isConnected => _connected;

  /// Change the target host (e.g. if a user manually enters an IP instead
  /// of relying on the SoftAP default).
  void setHost(String host) {
    _host = host;
  }

  /// Safe to call repeatedly and concurrently: overlapping calls share one
  /// in-flight attempt, and an already-working socket is left alone.
  /// Also (re)arms automatic reconnection until [pause] is called.
  @override
  Future<bool> connect() {
    _wantConnected = true;
    if (_connected && _channel != null) return Future.value(true);
    return _connecting ??= _doConnect().whenComplete(() => _connecting = null);
  }

  /// Stops reconnecting and closes the socket (used when switching to
  /// Cloud mode, so Local doesn't keep retrying in the background).
  void pause() {
    _wantConnected = false;
    _reconnectTimer?.cancel();
    _silenceTimer?.cancel();
    _gen++;
    _teardown();
    _setConnected(false);
  }

  Future<bool> _doConnect() async {
    _reconnectTimer?.cancel();
    final gen = ++_gen;
    _teardown();

    try {
      final channel = WebSocketChannel.connect(Uri.parse('ws://$_host/ws'));
      _channel = channel;
      _sub = channel.stream.listen(
        (raw) {
          if (gen != _gen) return;
          _onAlive();
          _handleMessage(raw);
        },
        onDone: () => _onClosed(gen),
        onError: (e) {
          print('[Local] WS error: $e');
          _onClosed(gen);
        },
        cancelOnError: false,
      );
      // WebSocketChannel.connect() doesn't await the handshake, so wait
      // for the channel's own `ready` instead of guessing a delay -- a
      // send issued before the socket is really open is silently lost.
      await channel.ready;
      if (gen != _gen) return false; // superseded by a newer attempt
      _backoffStep = 0;
      _onAlive();
      return true;
    } catch (e) {
      print('[Local] Connect error: $e');
      _onClosed(gen);
      return false;
    }
  }

  void _teardown() {
    _sub?.cancel();
    _sub = null;
    try { _channel?.sink.close(); } catch (_) {}
    _channel = null;
  }

  void _setConnected(bool value) {
    if (_connected == value) return;
    _connected = value;
    if (!_connectedController.isClosed) _connectedController.add(value);
  }

  // Anything arriving from the device proves it is reachable right now.
  void _onAlive() {
    _setConnected(true);
    _silenceTimer?.cancel();
    _silenceTimer = Timer(_silenceTimeout, () {
      print('[Local] No data for ${_silenceTimeout.inSeconds}s -- reconnecting');
      _onClosed(_gen);
    });
  }

  void _onClosed(int gen) {
    if (gen != _gen) return; // stale callback from a replaced socket
    if (_channel == null) return; // already handled (onError + onDone both fire)
    _silenceTimer?.cancel();
    _teardown();
    _setConnected(false);
    _scheduleReconnect();
  }

  void _scheduleReconnect() {
    if (!_wantConnected) return;
    _reconnectTimer?.cancel();
    final secs = _backoffSeconds[_backoffStep.clamp(0, _backoffSeconds.length - 1)];
    _backoffStep++;
    _reconnectTimer = Timer(Duration(seconds: secs), () {
      if (_wantConnected && !_connected) connect();
    });
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
        } else if (type == 'wifi_scan_result') {
          // Scan results arrive as a broadcast (not a direct command
          // response) since the firmware now kicks off the scan async
          // and pushes results once ready, rather than blocking the
          // command handler for the scan's duration. Re-wrap into the
          // same {"ok","cmd","data"} shape provisioning screens already
          // consume via responseStream, so nothing downstream needs two
          // separate code paths.
          _responseController.add({'ok': true, 'cmd': 'wifi_scan', 'data': data});
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
    _reconnectTimer?.cancel();
    _silenceTimer?.cancel();
    _wantConnected = false;
    _teardown();
  }
}
