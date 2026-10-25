import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/web_socket_channel.dart';

import 'node_link.dart';

/// Direct link to a node over the farm WiFi or its SoftAP (ws://HOST/ws).
/// Works with no internet and no broker.
class LocalLink with ReplyMatcher implements NodeLink {
  LocalLink(this.host);

  final String host;
  WebSocketChannel? _ch;
  StreamSubscription? _sub;
  bool _connected = false;
  bool _disposed = false;
  Timer? _retry;

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
    if (_connected) return true;
    try {
      final ch = WebSocketChannel.connect(Uri.parse('ws://$host/ws'));
      await ch.ready.timeout(const Duration(seconds: 4));
      _ch = ch;
      _sub = ch.stream.listen(_onMsg, onDone: _lost, onError: (_) => _lost(), cancelOnError: true);
      _setConnected(true);
      return true;
    } catch (_) {
      _setConnected(false);
      return false;
    }
  }

  void _onMsg(dynamic raw) {
    try {
      final m = jsonDecode(raw as String) as Map<String, dynamic>;
      if (m['type'] == 'state' && m['data'] is Map) {
        _state.add(Map<String, dynamic>.from(m['data']));
      } else if (m['type'] == 'event' && m['data'] is Map) {
        _events.add(Map<String, dynamic>.from(m['data']));
      } else {
        completeReply(m);
      }
    } catch (_) {}
  }

  void _lost() {
    _sub?.cancel();
    _ch = null;
    failAll('connection lost');
    _setConnected(false);
    if (!_disposed) {
      _retry?.cancel();
      _retry = Timer(const Duration(seconds: 3), connect);
    }
  }

  void _setConnected(bool v) {
    _connected = v;
    if (!_conn.isClosed) _conn.add(v);
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd,
      {Duration timeout = const Duration(seconds: 12)}) async {
    if (_ch == null) return {'ok': false, 'err': 'not connected'};
    final id = nextId();
    final msg = {...cmd, 'id': id, 'src': 'app'};
    final f = awaitReply(id, timeout);
    _ch!.sink.add(jsonEncode(msg));
    return f;
  }

  @override
  void dispose() {
    _disposed = true;
    _retry?.cancel();
    _sub?.cancel();
    try {
      _ch?.sink.close();
    } catch (_) {}
    _state.close();
    _events.close();
    _conn.close();
  }
}
