import 'dart:convert';
import 'package:http/http.dart' as http;

/// HTTP client for the test jig -- see PRODUCTION_TOOL_SPEC.md section
/// 6.3 and testing/jig_firmware/esp8266_wifi/src/main.cpp for the
/// firmware this talks to.
///
/// Reachable once the phone has joined the DUT's own SoftAP (the jig
/// auto-joins that same network -- see spec section 6.2), normally at
/// its mDNS name.
class JigService {
  final String host; // e.g. "fg1jig.local" or an IP

  JigService({this.host = 'fg1jig.local'});

  Uri _uri(String path, [Map<String, String>? query]) => Uri.http(host, path, query);

  Future<bool> ping() async {
    try {
      final resp = await http.get(_uri('/ping')).timeout(const Duration(seconds: 3));
      return resp.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  Future<Map<String, dynamic>?> status() async {
    try {
      final resp = await http.get(_uri('/status')).timeout(const Duration(seconds: 3));
      if (resp.statusCode != 200) return null;
      return jsonDecode(resp.body) as Map<String, dynamic>;
    } catch (_) {
      return null;
    }
  }

  /// True if the jig currently senses the DUT's relay output closed.
  Future<bool?> relayState() async {
    try {
      final resp = await http.get(_uri('/relay')).timeout(const Duration(seconds: 3));
      if (resp.statusCode != 200) return null;
      final body = jsonDecode(resp.body) as Map<String, dynamic>;
      return body['state'] == 'on';
    } catch (_) {
      return null;
    }
  }

  /// Emits exactly [count] pulses on the flow-sim output. Blocks
  /// (server-side) until done, so the returned future only completes
  /// once the pulses have actually been emitted.
  Future<bool> pulse(int count) async {
    try {
      final resp = await http
          .post(_uri('/pulse', {'n': count.toString()}))
          .timeout(Duration(seconds: 5 + (count / 200).ceil()));
      if (resp.statusCode != 200) return false;
      final body = jsonDecode(resp.body) as Map<String, dynamic>;
      return body['ok'] == true && body['emitted'] == count;
    } catch (_) {
      return false;
    }
  }
}
