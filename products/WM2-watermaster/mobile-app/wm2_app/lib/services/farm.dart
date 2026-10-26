import 'package:flutter/foundation.dart';

import 'node_store.dart';

/// Keeps a live link to every saved node for the farm dashboard.
class FarmController extends ChangeNotifier {
  FarmController(this.store) {
    store.addListener(sync);
    sync();
  }

  final NodeStore store;
  final Map<NodeEntry, NodeController> _ctls = {};
  bool _disposed = false;

  NodeController? ctl(NodeEntry e) => _ctls[e];

  void sync() {
    for (final e in store.nodes) {
      if (!_ctls.containsKey(e)) {
        final c = NodeController(e, store);
        c.addListener(_changed);
        _ctls[e] = c;
        c.connect();
      }
    }
    for (final e in _ctls.keys.toList()) {
      if (!store.nodes.contains(e)) {
        _ctls.remove(e)!
          ..removeListener(_changed)
          ..dispose();
      }
    }
    _changed();
  }

  Future<void> reconnectAll() async {
    for (final c in _ctls.values) {
      if (!c.connected) c.connect();
    }
  }

  void _changed() {
    if (!_disposed) notifyListeners();
  }

  /// Every valve on the farm with its node, for the banner and highlights.
  List<ValveRef> valves() {
    final out = <ValveRef>[];
    for (final e in store.nodes) {
      final s = _ctls[e]?.state;
      for (final v in (s?['valves'] as List? ?? [])) {
        out.add(ValveRef(e, Map<String, dynamic>.from(v)));
      }
    }
    return out;
  }

  @override
  void dispose() {
    _disposed = true;
    store.removeListener(sync);
    for (final c in _ctls.values) {
      c.removeListener(_changed);
      c.dispose();
    }
    super.dispose();
  }
}

class ValveRef {
  ValveRef(this.node, this.v);
  final NodeEntry node;
  final Map<String, dynamic> v;
  int get n => v['v'] ?? 1;
  bool get open => v['open'] == true;
  int get lastEnd => (v['last']?['end'] as num?)?.toInt() ?? 0;
  int? get nextDue => (v['next_due'] as num?)?.toInt();
}
