import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';

/// Talks to the DUT's Local WS API -- the exact same protocol
/// mobile-app/flutter_app/lib/services/local_service.dart and
/// testing/dut_client.py use ({"cmd": ...} in, {"ok":..,"cmd":..,
/// "data":..} or {"type":..,"data":..} out). See that file's header
/// comment for why AP+STA means we never need to leave this socket
/// mid-test even while checking the DUT's STA-side WiFi/MQTT join.
///
/// Assumes the phone is already joined to the DUT's own SoftAP
/// (192.168.4.1 is always its gateway IP in that mode).
class DutService {
  static const String defaultHost = '192.168.4.1';

  final String host;
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  final _responseController = StreamController<Map<String, dynamic>>.broadcast();
  bool connected = false;

  DutService({this.host = defaultHost});

  Future<bool> connect({Duration timeout = const Duration(seconds: 5)}) async {
    try {
      await _channel?.sink.close();
    } catch (_) {}
    _sub?.cancel();

    try {
      _channel = WebSocketChannel.connect(Uri.parse('ws://$host/ws'));
      _sub = _channel!.stream.listen(_handleMessage, onDone: () => connected = false,
          onError: (_) => connected = false, cancelOnError: false);
      await Future.delayed(const Duration(milliseconds: 300));
      connected = true;
      return true;
    } catch (_) {
      connected = false;
      return false;
    }
  }

  void _handleMessage(dynamic raw) {
    try {
      final msg = jsonDecode(raw as String) as Map<String, dynamic>;
      if (msg.containsKey('ok') || msg.containsKey('type')) {
        _responseController.add(msg);
      }
    } catch (_) {
      // ignore malformed frames
    }
  }

  void close() {
    _sub?.cancel();
    try {
      _channel?.sink.close();
    } catch (_) {}
    connected = false;
  }

  /// Sends {"cmd": cmd, ...extra} and waits for the next response whose
  /// "cmd" field matches (or, for device_info-style pushes, the next
  /// {"type":"status",...} broadcast) -- good enough for this tool's
  /// strictly-sequential command usage; not meant for concurrent calls.
  ///
  /// Auto-reconnects first if the last known state was disconnected --
  /// the firmware itself closes every local WS client the moment a
  /// background WiFi retry succeeds (main.cpp's pollBackgroundRetry(),
  /// well before it actually drops the SoftAP ~60s later per
  /// WIFI_STABLE_HOLD_MS), so a command sent right after `wifi_config`
  /// reconnects the DUT would otherwise silently go nowhere on the now-
  /// dead socket.
  Future<Map<String, dynamic>?> sendCommand(
    String cmd, {
    Map<String, dynamic> extra = const {},
    Duration timeout = const Duration(seconds: 8),
  }) async {
    if (!connected) {
      final reconnected = await connect();
      if (!reconnected) return null;
    }
    if (_channel == null) return null;
    final completer = Completer<Map<String, dynamic>?>();
    late StreamSubscription sub;
    sub = _responseController.stream.listen((msg) {
      if (msg['cmd'] == cmd && !completer.isCompleted) {
        completer.complete(msg['data'] as Map<String, dynamic>? ?? msg);
      }
    });
    _channel!.sink.add(jsonEncode({'cmd': cmd, ...extra}));
    try {
      return await completer.future.timeout(timeout);
    } catch (_) {
      return null;
    } finally {
      sub.cancel();
    }
  }

  Future<Map<String, dynamic>?> deviceInfo({Duration timeout = const Duration(seconds: 8)}) =>
      sendCommand('device_info', timeout: timeout);

  /// Polls device_info() until [predicate] is true or [timeout] elapses.
  Future<Map<String, dynamic>?> waitFor(
    bool Function(Map<String, dynamic> info) predicate, {
    Duration timeout = const Duration(seconds: 20),
    Duration pollInterval = const Duration(seconds: 1),
  }) async {
    final deadline = DateTime.now().add(timeout);
    Map<String, dynamic>? last;
    while (DateTime.now().isBefore(deadline)) {
      last = await deviceInfo();
      if (last != null && predicate(last)) return last;
      await Future.delayed(pollInterval);
    }
    return null;
  }
}
