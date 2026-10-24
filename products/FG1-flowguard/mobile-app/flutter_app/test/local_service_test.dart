import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:smart_water_controller/services/local_service.dart';

/// Minimal stand-in for the device's /ws endpoint.
class FakeDevice {
  late HttpServer server;
  final sockets = <WebSocket>[];
  int accepted = 0;

  Future<void> start([int port = 0]) async {
    server = await HttpServer.bind('127.0.0.1', port);
    server.listen((req) async {
      final ws = await WebSocketTransformer.upgrade(req);
      accepted++;
      sockets.add(ws);
      ws.listen((_) {}, onDone: () => sockets.remove(ws));
    });
  }

  int get port => server.port;
  void pushStatus() {
    for (final ws in sockets) {
      ws.add(jsonEncode({'type': 'status', 'data': {'device_id': 'SWC_001_B468'}}));
    }
  }
  void dropClients() {
    for (final ws in List.of(sockets)) { ws.close(); }
  }
  Future<void> stop() async {
    dropClients();
    await server.close(force: true);
  }
}

Future<void> until(bool Function() cond, {int seconds = 10}) async {
  final end = DateTime.now().add(Duration(seconds: seconds));
  while (!cond()) {
    if (DateTime.now().isAfter(end)) fail('condition not met in ${seconds}s');
    await Future.delayed(const Duration(milliseconds: 50));
  }
}

void main() {
  test('overlapping connect() calls share one socket and stay connected', () async {
    final dev = FakeDevice();
    await dev.start();
    final svc = LocalService(host: '127.0.0.1:${dev.port}');
    final results = await Future.wait([svc.connect(), svc.connect(), svc.connect()]);
    expect(results, [true, true, true]);
    await Future.delayed(const Duration(milliseconds: 500));
    expect(svc.isConnected, isTrue);
    expect(dev.accepted, 1);
    svc.dispose();
    await dev.stop();
  });

  test('reconnects automatically after the device drops the socket', () async {
    final dev = FakeDevice();
    await dev.start();
    final svc = LocalService(host: '127.0.0.1:${dev.port}');
    await svc.connect();
    expect(svc.isConnected, isTrue);
    dev.dropClients();
    await until(() => !svc.isConnected);
    await until(() => svc.isConnected); // backoff 2s, then back
    expect(dev.accepted, 2);
    svc.dispose();
    await dev.stop();
  });

  test('keeps retrying until the device appears, then connects', () async {
    final dev = FakeDevice();
    await dev.start();
    final port = dev.port;
    await dev.stop();
    final svc = LocalService(host: '127.0.0.1:$port');
    expect(await svc.connect(), isFalse);
    final dev2 = FakeDevice();
    await dev2.start(port);
    await until(() => svc.isConnected, seconds: 15);
    svc.dispose();
    await dev2.stop();
  });

  test('pause() stops reconnecting', () async {
    final dev = FakeDevice();
    await dev.start();
    final svc = LocalService(host: '127.0.0.1:${dev.port}');
    await svc.connect();
    svc.pause();
    expect(svc.isConnected, isFalse);
    await Future.delayed(const Duration(seconds: 4));
    expect(dev.accepted, 1);
    expect(svc.isConnected, isFalse);
    svc.dispose();
    await dev.stop();
  });

  test('status messages are delivered', () async {
    final dev = FakeDevice();
    await dev.start();
    final svc = LocalService(host: '127.0.0.1:${dev.port}');
    final got = Completer<String>();
    svc.statusStream.listen((s) => got.complete(s.deviceId));
    await svc.connect();
    await until(() => dev.sockets.isNotEmpty);
    dev.pushStatus();
    expect(await got.future.timeout(const Duration(seconds: 3)), 'SWC_001_B468');
    svc.dispose();
    await dev.stop();
  });
}
