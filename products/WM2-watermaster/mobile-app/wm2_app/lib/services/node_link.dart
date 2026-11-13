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

/// "3/4 min" for a valve: minutes run TODAY against today's plan (the
/// node's run_min). run.min is only the current open run, so it must not be
/// compared with the day's plan. A manual run shows the minutes it has left.
String valveProgress(Map v, {bool withManual = true}) {
  if (v['manual'] == true) {
    final left = ((v['manual_left_s'] as num?) ?? 0).toInt();
    if (left > 0) return 'manual, ${(left / 60).ceil()} min left';
    final l = (v['manual_left_l'] as num?) ?? 0;
    if (l > 0) return 'manual, ${l.round()} L left';
    return 'manual';
  }
  final m = ((v['manual_min'] as num?) ?? 0).toInt();
  final extra = withManual && m > 0 ? ' +$m manual' : '';
  if (v['planned_l'] != null) return '${v['litres'] ?? 0}/${v['planned_l']} L$extra';
  return '${v['run_min'] ?? 0}/${v['planned_min'] ?? '-'} min$extra';
}

/// " · manual" / " · program" for a run record that says which it was.
String runKind(Map? r) => r == null || r['manual'] == null ? '' : (r['manual'] == true ? ' · manual' : ' · program');
