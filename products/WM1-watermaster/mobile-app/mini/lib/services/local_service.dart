import 'dart:async';
import 'dart:convert';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'device_service.dart';
import 'network_binding.dart';
import '../models/device_status.dart';
import '../models/program.dart';

/// Talks directly to the device over its own SoftAP (or the same LAN,
/// once wifi_config has joined it to the farm's WiFi) — no broker
/// involved. Default host 192.168.4.1 is the ESP32's SoftAP gateway.
///
/// One WebSocket for both directions, same shape as FG1's LocalService:
/// incoming {"type": "...", "data": ...} broadcasts feed the streams,
/// outgoing commands use {"cmd": "...", ...}, and LocalServer replies
/// {"ok":..., "cmd":..., "data":...} directly to the requesting socket.
///
/// Two real bugs fixed here that FG1's own LocalService also has:
///   1. connect() used to declare success after a fixed 300ms delay,
///      regardless of whether the WebSocket handshake actually
///      completed. WebSocketChannel.connect() returns synchronously
///      without waiting for the handshake — on a real phone, if the
///      handshake silently hangs or fails (e.g. the OS routes this
///      app's traffic over cellular instead of the SoftAP's WiFi,
///      since that network has no internet), the app would still show
///      "Device Online" while nothing actually worked. Now awaits the
///      real `channel.ready` future, which only completes on an actual
///      successful handshake and throws otherwise.
///   2. A connection that hangs OPEN but silently stops receiving data
///      (same root cause, different symptom) would never trigger
///      onError/onDone, so it would never fixed on its own — now a
///      liveness watchdog forces a reconnect if no message (including
///      the periodic status broadcast) arrives for too long.
class LocalService implements DeviceService {
  static const String defaultHost = '192.168.4.1';
  static const Duration _reconnectDelay = Duration(seconds: 2);
  static const Duration _handshakeTimeout = Duration(seconds: 4);
  static const Duration _livenessTimeout = Duration(seconds: 15); // generous vs. firmware's 5s status heartbeat

  String _host;
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connected = false;
  bool _disposed = false;
  Timer? _reconnectTimer;
  Timer? _livenessTimer;
  DateTime? _lastMessageAt;

  final _statusController = StreamController<DeviceStatus>.broadcast();
  final _programsController = StreamController<List<Program>>.broadcast();
  final _libraryController = StreamController<List<LibrarySequence>>.broadcast();
  final _connectedController = StreamController<bool>.broadcast();
  final _responseController = StreamController<Map<String, dynamic>>.broadcast();
  // Human-readable connection lifecycle events — shown in the UI so
  // "why is it not working" doesn't require a serial monitor to answer.
  final _debugController = StreamController<String>.broadcast();

  LocalService({String host = defaultHost}) : _host = host;

  @override
  Stream<DeviceStatus> get statusStream => _statusController.stream;
  @override
  Stream<List<Program>> get programsStream => _programsController.stream;
  @override
  Stream<List<LibrarySequence>> get libraryStream => _libraryController.stream;
  @override
  Stream<bool> get connectedStream => _connectedController.stream;
  @override
  Stream<bool> get deviceOnlineStream => _connectedController.stream;
  Stream<Map<String, dynamic>> get responseStream => _responseController.stream;
  // Live updates only — a broadcast stream doesn't replay anything that
  // fired before a listener attached. Since this service starts
  // connecting the instant the app launches (see DashboardScreen), any
  // UI that only subscribes when its screen opens would miss the whole
  // startup sequence. logHistory below is the fix: buffered inside the
  // service itself, independent of whether anything was ever listening.
  Stream<String> get debugStream => _debugController.stream;
  List<String> get logHistory => List.unmodifiable(_logHistory);

  @override
  bool get isConnected => _connected;

  void setHost(String host) => _host = host;
  String get host => _host;

  final List<String> _logHistory = [];
  static const int _maxLogHistory = 30;

  void _log(String msg) {
    print('[Local] $msg');
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    final line = '${two(now.hour)}:${two(now.minute)}:${two(now.second)}  $msg';
    _logHistory.add(line);
    if (_logHistory.length > _maxLogHistory) _logHistory.removeAt(0);
    _debugController.add(line);
  }

  @override
  Future<bool> connect() => _connectInternal();

  /// Explicit user-triggered retry, wired to the Dashboard/Settings
  /// "Retry" buttons. Functionally the same as connect() now that
  /// binding is cheap/idempotent either way — kept as its own method so
  /// call sites read as "the user asked for this" vs. "the internal
  /// loop is trying again".
  Future<bool> retryNow() => _connectInternal();

  Future<bool> _connectInternal() async {
    _reconnectTimer?.cancel();
    if (_connected && _channel != null) return true;

    try { await _channel?.sink.close(); } catch (_) {}
    _sub?.cancel();
    _livenessTimer?.cancel();

    // The whole attempt is wrapped in one try/finally: whatever happens
    // inside (bind fails, handshake times out, an unexpected exception),
    // the finally block below is the single place that decides whether
    // to retry — no individual failure path can forget to reschedule
    // and silently let the retry loop die. (Confirmed as a real
    // possibility: a user reported the app never recovering on its own
    // after opening it before joining the device's WiFi — this removes
    // any code path that could cause that regardless of the exact cause.)
    try {
      // Ensures the process is bound to a WiFi network before even
      // trying to open the socket — otherwise, on a phone that routes
      // app traffic over mobile data instead of a no-internet WiFi
      // network, the WebSocket connect attempt below goes out the wrong
      // interface and never reaches the device at all (confirmed:
      // firmware sees the phone join the AP but the WebSocket never
      // arrives). Safe to call on every attempt, including from the
      // automatic retry loop: MainActivity.kt registers its network
      // callback once for the whole app lifetime and re-binds on its
      // own whenever the WiFi network changes — this call is just a
      // cheap check of that, not a fresh OS-level registration each
      // time. See MainActivity.kt / network_binding.dart.
      final bound = await NetworkBinding.bindWifi();
      _log(bound ? 'Bound to WiFi network' : 'Could not bind to a WiFi network — is the phone actually on one?');

      _log('Connecting to ws://$_host/ws ...');
      final channel = WebSocketChannel.connect(Uri.parse('ws://$_host/ws'));
      _channel = channel;

      // This is the actual fix: wait for the real handshake to complete
      // (or fail/time out) before believing we're connected at all.
      await channel.ready.timeout(_handshakeTimeout);

      _sub = channel.stream.listen(
        _handleMessage,
        onDone: () {
          _log('Connection closed by device');
          _connected = false;
          _connectedController.add(false);
          _scheduleReconnect();
        },
        onError: (e) {
          _log('WS error: $e');
          _connected = false;
          _connectedController.add(false);
          _scheduleReconnect();
        },
        cancelOnError: false,
      );

      _connected = true;
      _lastMessageAt = DateTime.now();
      _connectedController.add(true);
      _log('Connected');
      _startLivenessWatchdog();
      return true;
    } catch (e) {
      // The failure mode this specifically catches: phone shows
      // "connected to WM1_XXXX WiFi" but the handshake to 192.168.4.1
      // never completes — most commonly because the phone's OS is
      // routing this app's traffic over mobile data instead of that
      // WiFi (it has no internet, so Android/iOS may prefer another
      // network for general traffic), or because the phone simply
      // wasn't on the device's WiFi yet when this attempt started.
      _log('Connect failed: $e');
      _connected = false;
      _connectedController.add(false);
      return false;
    } finally {
      if (!_connected) _scheduleReconnect();
    }
  }


  void _startLivenessWatchdog() {
    _livenessTimer?.cancel();
    _livenessTimer = Timer.periodic(const Duration(seconds: 5), (_) {
      if (!_connected || _lastMessageAt == null) return;
      final silentFor = DateTime.now().difference(_lastMessageAt!);
      if (silentFor > _livenessTimeout) {
        _log('No data for ${silentFor.inSeconds}s — connection looks dead, reconnecting');
        _connected = false;
        _connectedController.add(false);
        _livenessTimer?.cancel();
        try { _channel?.sink.close(); } catch (_) {}
        _scheduleReconnect();
      }
    });
  }

  void _scheduleReconnect() {
    if (_disposed) return;
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(_reconnectDelay, () {
      if (!_disposed && !_connected) connect();
    });
  }

  void _handleMessage(dynamic raw) {
    _lastMessageAt = DateTime.now();
    try {
      final msg = jsonDecode(raw as String) as Map<String, dynamic>;

      if (msg.containsKey('type')) {
        final type = msg['type'];
        final data = msg['data'];
        if (type == 'status' && data is Map<String, dynamic>) {
          _statusController.add(DeviceStatus.fromJson(data));
        } else if (type == 'programs' && data is List) {
          _programsController.add(
              data.map((e) => Program.fromJson(e as Map<String, dynamic>)).toList());
        } else if (type == 'library' && data is List) {
          _libraryController.add(
              data.map((e) => LibrarySequence.fromJson(e as Map<String, dynamic>)).toList());
        } else if (type == 'wifi_scan_result') {
          // Re-wrapped into the same {ok,cmd,data} shape direct command
          // responses use, same as FG1 — the scan itself is async on the
          // firmware side (see WiFiScanner.h), so its result arrives as
          // a broadcast rather than a direct reply to the wifi_scan call.
          _responseController.add({'ok': true, 'cmd': 'wifi_scan', 'data': data});
        }
        return;
      }

      if (msg.containsKey('ok')) {
        _responseController.add(msg);
        if (msg['cmd'] == 'get_status' && msg['data'] is Map<String, dynamic>) {
          _statusController.add(DeviceStatus.fromJson(msg['data'] as Map<String, dynamic>));
        } else if (msg['cmd'] == 'list_programs' && msg['data'] is List) {
          _programsController.add((msg['data'] as List)
              .map((e) => Program.fromJson(e as Map<String, dynamic>))
              .toList());
        } else if (msg['cmd'] == 'list_sequence_library' && msg['data'] is List) {
          _libraryController.add((msg['data'] as List)
              .map((e) => LibrarySequence.fromJson(e as Map<String, dynamic>))
              .toList());
        }
      }
    } catch (e) {
      _log('Parse error: $e');
    }
  }

  void _send(Map<String, dynamic> payload) {
    if (_channel == null || !_connected) {
      _log('Not connected — "${payload['cmd']}" was NOT sent');
      return;
    }
    try {
      _channel!.sink.add(jsonEncode(payload));
      _log('Sent: ${payload['cmd']}');
    } catch (e) {
      _log('Send error: $e');
    }
  }

  @override
  void sendRaw(Map<String, dynamic> payload) => _send(payload);

  @override
  void manualSet(String channel, bool state) =>
      _send({'cmd': 'manual_set', 'channel': channel, 'state': state});
  @override
  void triggerProgram(int programId, int seqIndex) =>
      _send({'cmd': 'trigger_program', 'id': programId, 'seq': seqIndex});
  @override
  void forceStop() => _send({'cmd': 'force_stop'});
  @override
  void pause() => _send({'cmd': 'pause'});
  @override
  void resume() => _send({'cmd': 'resume'});
  @override
  void getPrograms() => _send({'cmd': 'list_programs'});
  @override
  void setPrograms(List<Program> programs) =>
      _send({'cmd': 'set_programs', 'programs': programs.map((p) => p.toJson()).toList()});
  @override
  void getLibrary() => _send({'cmd': 'list_sequence_library'});
  @override
  void setLibrary(List<LibrarySequence> library) =>
      _send({'cmd': 'set_sequence_library', 'sequences': library.map((e) => e.toJson()).toList()});
  @override
  void simulatePowerLoss() => _send({'cmd': 'simulate_power_loss'});
  @override
  void simulatePowerRestore() => _send({'cmd': 'simulate_power_restore'});

  void getStatus() => _send({'cmd': 'get_status'});
  void scanWifi() => _send({'cmd': 'wifi_scan'});
  // Local-only for now (see history_screen.dart) — reply arrives on
  // responseStream like device_info/wifi_scan, cmd == 'get_history'.
  void getHistory({int since = 0, int max = 500}) => _send({'cmd': 'get_history', 'since': since, 'max': max});
  void syncRtcFromPhone() {
    final now = DateTime.now();
    // Same FG1 convention: reinterpret the phone's LOCAL wall-clock
    // fields as if they were UTC before sending — the RTC stores raw
    // wall-clock, not a true UTC epoch. See firmware's Ds1307Clock.h.
    final asUtc = DateTime.utc(now.year, now.month, now.day, now.hour, now.minute, now.second);
    _send({'cmd': 'rtc_sync', 'unix': asUtc.millisecondsSinceEpoch ~/ 1000});
  }
  void factoryReset() => _send({'cmd': 'factory_reset'});
  void setRelayNames({
    required String pump,
    required String dosing,
    required List<String> valves,
    required String pressure1,
    required String pressure2,
    required String flow,
    required String waterUpper,
    required String waterLower,
  }) =>
      _send({
        'cmd': 'set_relay_names',
        'pump': pump,
        'dosing': dosing,
        'valves': valves,
        'pressure1': pressure1,
        'pressure2': pressure2,
        'flow': flow,
        'waterUpper': waterUpper,
        'waterLower': waterLower,
      });

  @override
  void dispose() {
    _disposed = true;
    _reconnectTimer?.cancel();
    _livenessTimer?.cancel();
    _statusController.close();
    _programsController.close();
    _libraryController.close();
    _connectedController.close();
    _responseController.close();
    _debugController.close();
    _sub?.cancel();
    try { _channel?.sink.close(); } catch (_) {}
  }
}
