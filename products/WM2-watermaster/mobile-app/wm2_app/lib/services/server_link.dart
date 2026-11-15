import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import 'account.dart';
import 'node_link.dart';
import 'sse.dart';

/// Link to a node through the AgriSense server (signed in): commands are
/// POSTed and relayed by the server, which checks the farm access; state,
/// events and online/offline come as a server-sent event stream. No broker
/// login in the app.
class ServerLink implements NodeLink {
  ServerLink(this.deviceId);

  final String deviceId;
  SseConnection? _sse;
  bool _connected = false;
  bool _nodeOnline = false;
  bool _disposed = false;

  final _state = StreamController<Map<String, dynamic>>.broadcast();
  final _events = StreamController<Map<String, dynamic>>.broadcast();
  final _conn = StreamController<bool>.broadcast();

  @override
  String get kind => 'cloud';
  @override
  Stream<Map<String, dynamic>> get state => _state.stream;
  @override
  Stream<Map<String, dynamic>> get events => _events.stream;
  @override
  Stream<bool> get connected => _conn.stream;
  @override
  bool get isConnected => _connected;

  /// The node itself is connected to the cloud (its last will says online).
  bool get nodeOnline => _nodeOnline;

  String get _url => '${Account.base}/api/app/devices/$deviceId';

  @override
  Future<bool> connect() async {
    if (_connected) return true;
    final first = Completer<bool>();
    _sse = openSse('$_url/live', Account.instance.headers, (event, data) {
      if (!first.isCompleted) first.complete(true);
      _set(true);
      try {
        final j = jsonDecode(data);
        if (event == 'status' && j is Map) {
          _nodeOnline = j['online'] == true;
          if (!_conn.isClosed) _conn.add(_connected);
        } else if (event == 'state' && j is Map) {
          _state.add(Map<String, dynamic>.from(j));
        } else if (event == 'event' && j is Map) {
          _events.add(Map<String, dynamic>.from(j));
        }
      } catch (_) {}
    }, (err) {
      if (!first.isCompleted) first.complete(false);
      _set(false);
    });
    return first.future.timeout(const Duration(seconds: 10), onTimeout: () => false);
  }

  void _set(bool v) {
    if (_disposed || _connected == v) return;
    _connected = v;
    if (!_conn.isClosed) _conn.add(v);
  }

  @override
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd, {Duration timeout = const Duration(seconds: 20)}) async {
    final secs = timeout.inSeconds.clamp(3, 30);
    try {
      final r = await http
          .post(Uri.parse('$_url/cmd?timeout=$secs'), headers: Account.instance.headers, body: jsonEncode(cmd))
          .timeout(Duration(seconds: secs + 5));
      final j = jsonDecode(r.body);
      if (r.statusCode == 401) Account.instance.devices(); // marks signed out
      return j is Map ? Map<String, dynamic>.from(j) : {'ok': false, 'err': 'bad reply from the server'};
    } catch (_) {
      return {'ok': false, 'err': 'no reply from the server (internet?)'};
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _sse?.close();
    _state.close();
    _events.close();
    _conn.close();
  }
}
