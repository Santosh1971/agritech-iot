// The Bluetooth link to one ASC kit: the firmware's Nordic-UART-style
// service carrying the same JSON lines as USB.
import 'dart:async';
import 'dart:convert';

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import 'protocol.dart';

final serviceUuid = Guid('6E400001-B5A3-F393-E0A9-E50E24DCCA9E');
final rxUuid = Guid('6E400002-B5A3-F393-E0A9-E50E24DCCA9E'); // we write here
final txUuid = Guid('6E400003-B5A3-F393-E0A9-E50E24DCCA9E'); // the board notifies here

class BoardLink {
  final BluetoothDevice device;
  BluetoothCharacteristic? _rx;
  StreamSubscription<List<int>>? _sub;
  final _buffer = LineBuffer();
  final _messages = StreamController<Map<String, dynamic>>.broadcast();
  final List<String> raw = []; // last lines, for Engineer's view

  BoardLink(this.device);

  Stream<Map<String, dynamic>> get messages => _messages.stream;

  Future<void> connect() async {
    await device.connect(timeout: const Duration(seconds: 15), autoConnect: false);
    try {
      await device.requestMtu(247);
    } catch (_) {
      // Not every phone allows a larger MTU; chunks just get smaller.
    }
    final services = await device.discoverServices();
    final svc = services.firstWhere((s) => s.uuid == serviceUuid, orElse: () => throw Exception('This is not an ASC kit.'));
    _rx = svc.characteristics.firstWhere((c) => c.uuid == rxUuid);
    final tx = svc.characteristics.firstWhere((c) => c.uuid == txUuid);
    _sub = tx.onValueReceived.listen((chunk) {
      for (final line in _buffer.add(chunk)) {
        raw.add(line);
        if (raw.length > 60) raw.removeAt(0);
        try {
          final m = jsonDecode(line);
          if (m is Map<String, dynamic>) _messages.add(m);
        } catch (_) {
          // a boot message or a half line: ignore
        }
      }
    });
    device.cancelWhenDisconnected(_sub!);
    await tx.setNotifyValue(true);
  }

  Future<void> send(Map<String, dynamic> command) async {
    final rx = _rx;
    if (rx == null) throw Exception('Not connected.');
    for (final chunk in chunksFor(command, device.mtuNow)) {
      await rx.write(chunk, withoutResponse: false);
    }
  }

  /// Sends a command and waits for the reply of [type]. Throws the board's
  /// own words when it refuses (for example "Press the PAIR button…").
  Future<Map<String, dynamic>> request(Map<String, dynamic> command, String type, {Duration timeout = const Duration(seconds: 6)}) async {
    final reply = messages.firstWhere((m) => m['type'] == type || m['type'] == 'error').timeout(timeout);
    await send(command);
    final m = await reply;
    if (m['ok'] == false || m['type'] == 'error') throw Exception(m['error'] ?? 'The board refused that.');
    return m;
  }

  Future<void> disconnect() async {
    await _sub?.cancel();
    await device.disconnect();
    await _messages.close();
  }
}
