import 'dart:async';
import 'dart:convert';
import 'dart:io';

/// Asks every address of the phone's WiFi /24 network for /api/state and
/// returns the IP of the node whose "dev" matches [deviceId], or null.
/// Takes about 2-4 s on a typical farm router.
Future<String?> findNodeOnLan(String deviceId) async {
  if (deviceId.isEmpty) return null;
  final prefixes = <String>{};
  try {
    for (final nic in await NetworkInterface.list(type: InternetAddressType.IPv4)) {
      for (final a in nic.addresses) {
        final p = a.address.split('.');
        if (p.length != 4 || a.isLoopback) continue;
        if (p[0] == '192' && p[1] == '168' && p[2] == '4') continue; // a node's SoftAP, not the farm WiFi
        prefixes.add('${p[0]}.${p[1]}.${p[2]}');
      }
    }
  } catch (_) {
    return null;
  }
  for (final prefix in prefixes) {
    final hit = await _sweep(prefix, deviceId);
    if (hit != null) return hit;
  }
  return null;
}

Future<String?> _sweep(String prefix, String deviceId) async {
  final client = HttpClient()..connectionTimeout = const Duration(milliseconds: 1200);
  final found = Completer<String?>();
  var pending = 0;
  var next = 1;
  void launch() {
    while (pending < 48 && next <= 254 && !found.isCompleted) {
      final ip = '$prefix.${next++}';
      pending++;
      _probe(client, ip, deviceId).then((ok) {
        pending--;
        if (ok && !found.isCompleted) found.complete(ip);
        if (!found.isCompleted) {
          if (next <= 254) {
            launch();
          } else if (pending == 0) {
            found.complete(null);
          }
        }
      });
    }
  }

  launch();
  final r = await found.future.timeout(const Duration(seconds: 15), onTimeout: () => null);
  client.close(force: true);
  return r;
}

Future<bool> _probe(HttpClient client, String ip, String deviceId) async {
  try {
    final req = await client.getUrl(Uri.parse('http://$ip/api/state'));
    final res = await req.close().timeout(const Duration(seconds: 2));
    if (res.statusCode != 200) {
      await res.drain<void>();
      return false;
    }
    final body = await res.transform(utf8.decoder).join().timeout(const Duration(seconds: 2));
    final j = jsonDecode(body);
    return j is Map && j['dev'] == deviceId;
  } catch (_) {
    return false;
  }
}
