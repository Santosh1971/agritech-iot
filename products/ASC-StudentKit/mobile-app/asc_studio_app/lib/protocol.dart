// The ASC kit protocol and the student's app layout, with no Bluetooth or
// Flutter in it, so it can be unit-tested (test/protocol_test.dart).
//
// The board speaks one JSON object per line (see
// products/ASC-StudentKit/firmware/README.md). The layout is designed in the
// studio's App stage (webapp lib/studio/appLayout.ts) and travels inside the
// design the board stores; the app asks for it with {"cmd":"get_design"}.
import 'dart:convert';

/// Turns a stream of byte chunks (BLE notifications) into complete lines.
class LineBuffer {
  final List<int> _bytes = [];

  /// Adds a chunk and returns every line it completed.
  List<String> add(List<int> chunk) {
    final lines = <String>[];
    for (final b in chunk) {
      if (b == 10) {
        final line = utf8.decode(_bytes, allowMalformed: true).trim();
        _bytes.clear();
        if (line.isNotEmpty) lines.add(line);
      } else if (_bytes.length < 16384) {
        _bytes.add(b);
      }
    }
    return lines;
  }
}

/// Splits one command line into chunks that fit a BLE write of [mtu].
List<List<int>> chunksFor(Map<String, dynamic> command, int mtu) {
  final bytes = utf8.encode('${jsonEncode(command)}\n');
  final size = mtu > 3 ? mtu - 3 : 20;
  return [for (var i = 0; i < bytes.length; i += size) bytes.sublist(i, i + size > bytes.length ? bytes.length : i + size)];
}

enum TileKind { value, graph, gauge, toggle, lamp }

TileKind _kind(String? k) => switch (k) {
      'graph' => TileKind.graph,
      'gauge' => TileKind.gauge,
      'switch' => TileKind.toggle,
      'lamp' => TileKind.lamp,
      _ => TileKind.value,
    };

class Tile {
  final String ref; // "S1", "S3:t" or "OUT1"
  final TileKind kind;
  final String label;
  final String labelHi;
  const Tile(this.ref, this.kind, this.label, this.labelHi);

  bool get isOutput => ref.startsWith('OUT');
  String labelFor(bool hindi) => hindi && labelHi.isNotEmpty ? labelHi : label;
}

class Alert {
  final String ref;
  final bool below;
  final double value;
  final String say;
  const Alert(this.ref, this.below, this.value, this.say);

  /// True when [v] is past the alert level. A missing reading never alerts.
  bool firing(double? v) => v != null && (below ? v < value : v > value);
}

class AppLayout {
  final String name;
  final bool hindi;
  final List<Tile> tiles;
  final List<Alert> alerts;
  const AppLayout(this.name, this.hindi, this.tiles, this.alerts);

  /// Reads the layout out of a `design` reply. Boards whose design has no
  /// layout get a plain one: every reading as a number, every output as a switch.
  static AppLayout fromDesign(Map<String, dynamic>? design) {
    final app = design?['app'];
    if (app is Map<String, dynamic>) {
      return AppLayout(
        (app['name'] as String?) ?? (design?['name'] as String?) ?? 'ASC kit',
        app['lang'] == 'hi',
        [
          for (final t in (app['tiles'] as List? ?? const []))
            if (t is Map) Tile('${t['ref']}', _kind(t['kind'] as String?), '${t['label'] ?? t['ref']}', '${t['labelHi'] ?? ''}'),
        ],
        [
          for (final a in (app['alerts'] as List? ?? const []))
            if (a is Map) Alert('${a['ref']}', a['when'] != 'above', (a['value'] as num?)?.toDouble() ?? 0, '${a['say'] ?? ''}'),
        ],
      );
    }
    final ports = (design?['ports'] as Map?)?.cast<String, dynamic>() ?? const {};
    return AppLayout(
      (design?['name'] as String?) ?? 'ASC kit',
      false,
      [
        for (final e in ports.entries)
          Tile(e.key, e.key.startsWith('OUT') ? TileKind.toggle : TileKind.value, '${e.value} (${e.key})', ''),
      ],
      const [],
    );
  }
}

/// One `live` frame from the board.
class Live {
  final Map<String, double?> values;
  final Map<String, bool> outputs;
  const Live(this.values, this.outputs);

  static Live? fromMessage(Map<String, dynamic> m) {
    if (m['type'] != 'live' || m['values'] is! Map) return null;
    return Live(
      {for (final e in (m['values'] as Map).entries) '${e.key}': (e.value as num?)?.toDouble()},
      {for (final e in (m['outputs'] as Map? ?? const {}).entries) '${e.key}': e.value == 1},
    );
  }

  double? valueOf(String ref) => values[ref] ?? (outputs.containsKey(ref) ? (outputs[ref]! ? 1 : 0) : null);
}
