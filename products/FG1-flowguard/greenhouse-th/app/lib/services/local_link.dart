import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';

import 'node_link.dart';

/// Direct link to a controller over its SoftAP (192.168.4.1) or the farm
/// WiFi (its LAN IP): ws://HOST/ws. Works with no internet.
///
/// Firmware messages:
///   {"type":"status","data":{...}}            pushed every 5 s
///   {"type":"wifi_scan_result","data":[...]}  after a wifi_scan
///   {"ok":..,"cmd":..,"id":..,"error":..,"data":..}   command reply
class LocalLink with ReplyMatcher implements NodeLink {
  LocalLink(this.host);

  final String host;
  WebSocketChannel? _ch;
  StreamSubscription? _sub;
  bool _connected = false;
  bool _disposed = false;
  Timer? _retry;
  Timer? _silence;

  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final _conn = StreamController<bool>.broadcast();

  @override
  String get kind => 'Local';
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
    if (_connected || _disposed) return _connected;
    try {
      final ch = WebSocketChannel.connect(Uri.parse('ws://$host/ws'));
      await ch.ready.timeout(const Duration(seconds: 4));
      _ch = ch;
      _sub = ch.stream.listen(_onMsg, onDone: _lost, onError: (_) => _lost(), cancelOnError: true);
      _setConnected(true);
      _alive();
      return true;
    } catch (_) {
      _setConnected(false);
      _scheduleRetry();
      return false;
    }
  }

  // Status arrives every 5 s; 15 s of silence means the link is dead.
  void _alive() {
    _silence?.cancel();
    _silence = Timer(const Duration(seconds: 15), _lost);
  }

  void _onMsg(dynamic raw) {
    _alive();
    try {
      final m = jsonDecode(raw as String) as Map<String, dynamic>;
      if (m['type'] == 'status' && m['data'] is Map) {
        _state.add(Map<String, dynamic>.from(m['data']));
      } else if (m['type'] != null) {
        _events.add(m);
      } else {
        if (m['error'] != null) m['err'] = m['error'];
        completeReply(m);
      }
    } catch (_) {}
  }

  void _lost() {
    _silence?.cancel();
    _sub?.cancel();
    try {
      _ch?.sink.close();
    } catch (_) {}
    _ch = null;
    failAll('connection lost');
    _setConnected(false);
    _scheduleRetry();
  }

  void _scheduleRetry() {
    if (_disposed) return;
    _retry?.cancel();
    _retry = Timer(const Duration(seconds: 4), connect);
  }

  void _setConnected(bool v) {
    if (_connected == v) return;
    _connected = v;
    if (!_conn.isClosed) _conn.add(v);
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd,
      {Duration timeout = const Duration(seconds: 10)}) async {
    if (_ch == null) return {'ok': false, 'err': 'not connected'};
    final id = nextId();
    final f = awaitReply(id, timeout);
    _ch!.sink.add(jsonEncode({...cmd, 'id': id}));
    return f;
  }

  @override
  void dispose() {
    _disposed = true;
    _retry?.cancel();
    _silence?.cancel();
    _sub?.cancel();
    try {
      _ch?.sink.close();
    } catch (_) {}
    _state.close();
    _events.close();
    _conn.close();
  }
}
