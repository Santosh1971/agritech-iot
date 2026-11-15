import 'dart:async';

/// One way of talking to a WM2 node. Both transports carry the same JSON:
/// commands {"cmd":..,"id":..} and replies {"id":..,"ok":..,"data":..}, plus
/// pushed state snapshots and events.
abstract class NodeLink {
  String get kind; // "Local" or "MQTT"
  Stream<Map<String, dynamic>> get state;
  Stream<Map<String, dynamic>> get events;
  Stream<bool> get connected;
  bool get isConnected;

  Future<bool> connect();

  /// Sends a command and waits for its reply. Never throws: on timeout or
  /// disconnect returns {"ok":false,"err":...}.
  Future<Map<String, dynamic>> send(Map<String, dynamic> cmd, {Duration timeout});

  void dispose();
}

/// Shared bookkeeping for matching replies to commands.
mixin ReplyMatcher {
  final Map<String, Completer<Map<String, dynamic>>> _pending = {};
  int _next = 1;

  String nextId() => 'a${DateTime.now().millisecondsSinceEpoch % 100000}-${_next++}';

  Future<Map<String, dynamic>> awaitReply(String id, Duration timeout) {
    final c = Completer<Map<String, dynamic>>();
    _pending[id] = c;
    return c.future.timeout(timeout, onTimeout: () {
      _pending.remove(id);
      return {'ok': false, 'err': 'no reply from node (timeout)'};
    });
  }

  /// Returns true if `msg` was a reply we were waiting for.
  bool completeReply(Map<String, dynamic> msg) {
    final id = msg['id'];
    if (id is String && _pending.containsKey(id)) {
      _pending.remove(id)!.complete(msg);
      return true;
    }
    return false;
  }

  void failAll(String err) {
    for (final c in _pending.values) {
      if (!c.isCompleted) c.complete({'ok': false, 'err': err});
    }
    _pending.clear();
  }
}
