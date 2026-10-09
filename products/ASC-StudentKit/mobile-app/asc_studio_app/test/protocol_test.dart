import 'dart:convert';

import 'package:asc_studio_app/protocol.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('LineBuffer joins BLE chunks into lines', () {
    final b = LineBuffer();
    expect(b.add(utf8.encode('{"type":"hel')), isEmpty);
    expect(b.add(utf8.encode('lo"}\n{"type":"live"')), ['{"type":"hello"}']);
    expect(b.add(utf8.encode('}\n')), ['{"type":"live"}']);
  });

  test('chunksFor splits a command to fit the MTU and ends with a newline', () {
    final cmd = {'cmd': 'config', 'config': {'name': 'x' * 100}};
    final chunks = chunksFor(cmd, 23);
    expect(chunks.every((c) => c.length <= 20), isTrue);
    expect(utf8.decode(chunks.expand((c) => c).toList()), '${jsonEncode(cmd)}\n');
  });

  test('layout comes from the design, Hindi labels included', () {
    final l = AppLayout.fromDesign({
      'name': 'Nursery',
      'ports': {'S1': 'soil', 'OUT1': 'pump'},
      'app': {
        'name': 'Nursery Guard',
        'lang': 'hi',
        'tiles': [
          {'ref': 'S1', 'kind': 'graph', 'label': 'Soil moisture', 'labelHi': 'मिट्टी की नमी'},
          {'ref': 'OUT1', 'kind': 'switch', 'label': 'Pump', 'labelHi': 'पंप'},
        ],
        'alerts': [
          {'ref': 'S2', 'when': 'below', 'value': 0.5, 'say': 'Tank is empty'},
        ],
      },
    });
    expect(l.name, 'Nursery Guard');
    expect(l.hindi, isTrue);
    expect(l.tiles.map((t) => t.kind), [TileKind.graph, TileKind.toggle]);
    expect(l.tiles.first.labelFor(true), 'मिट्टी की नमी');
    expect(l.tiles[1].isOutput, isTrue);
    expect(l.alerts.single.firing(0), isTrue);
    expect(l.alerts.single.firing(1), isFalse);
    expect(l.alerts.single.firing(null), isFalse);
  });

  test('a design without a layout still gets a usable screen', () {
    final l = AppLayout.fromDesign({'name': 'Kit', 'ports': {'S1': 'soil', 'OUT1': 'pump'}});
    expect(l.tiles.map((t) => t.kind), [TileKind.value, TileKind.toggle]);
  });

  test('live frames are read, and acknowledgements are not mistaken for them', () {
    expect(Live.fromMessage({'type': 'live', 'ok': true}), isNull);
    final live = Live.fromMessage({
      'type': 'live',
      'values': {'S1': 41.5, 'S3:t': null},
      'outputs': {'OUT1': 1},
    })!;
    expect(live.valueOf('S1'), 41.5);
    expect(live.valueOf('S3:t'), isNull);
    expect(live.valueOf('OUT1'), 1);
  });

  test('tiles carry their unit from the design', () {
    final l = AppLayout.fromDesign({
      'name': 'Kit',
      'ports': {'S1': 'dht'},
      'app': {'name': 'Kit', 'tiles': [{'ref': 'S1:t', 'kind': 'value', 'label': 'Air temperature', 'unit': '°C'}, {'ref': 'OUT1', 'kind': 'switch', 'label': 'Pump'}]},
    });
    expect(l.tiles[0].unit, '°C');
    expect(l.tiles[1].unit, '');
  });
}
