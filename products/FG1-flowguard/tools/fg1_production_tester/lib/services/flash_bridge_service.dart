import 'dart:async';
import 'dart:convert';
import 'package:http/http.dart' as http;

/// HTTP client for the laptop-side Flash Bridge -- see
/// docs/testing/PRODUCTION_TOOL_SPEC.md section 7 and
/// testing/flash_bridge.py for the server this talks to.
///
/// Only reachable while the phone is on the Bench LAN (see the session
/// flow in section 5) -- before the DUT has been flashed and brought up
/// its own SoftAP.
class FlashBridgeService {
  final String host; // e.g. "192.168.1.50:8787"

  FlashBridgeService({required this.host});

  Uri _uri(String path, [Map<String, String>? query]) =>
      Uri.http(host, path, query);

  Future<Map<String, dynamic>?> health() async {
    try {
      final resp = await http.get(_uri('/health')).timeout(const Duration(seconds: 5));
      if (resp.statusCode != 200) return null;
      return jsonDecode(resp.body) as Map<String, dynamic>;
    } catch (_) {
      return null;
    }
  }

  /// Streams flash progress. Each emitted value is either:
  ///   {"type": "log", "line": "..."}
  ///   {"type": "result", "passed": true|false}
  /// The stream closes right after the "result" event.
  /// Never throws -- a connection failure (e.g. the phone is on the
  /// wrong WiFi network and can't reach the bridge at all) is surfaced
  /// as a {"type":"log",...} line followed by a {"type":"result",
  /// "passed":false} event, same shape as a real flash failure, so
  /// callers don't need a separate error-handling path.
  Stream<Map<String, dynamic>> flash({
    required String env,
    String? port,
    int timeoutS = 120,
  }) async* {
    final query = {'env': env, if (port != null && port.isNotEmpty) 'port': port};
    final request = http.Request('POST', _uri('/flash', query));
    final client = http.Client();
    try {
      final streamedResponse = await client.send(request).timeout(Duration(seconds: timeoutS + 15));
      final lines = streamedResponse.stream.transform(utf8.decoder).transform(const LineSplitter());
      await for (final line in lines) {
        if (line.trim().isEmpty) continue;
        yield jsonDecode(line) as Map<String, dynamic>;
      }
    } catch (e) {
      yield {
        'type': 'log',
        'line': "Could not reach Flash Bridge at $host ($e) -- is the phone on the Bench LAN?"
      };
      yield {'type': 'result', 'passed': false};
    } finally {
      client.close();
    }
  }

  Future<Map<String, dynamic>> bootLog({required String port, double windowS = 10.0}) async {
    try {
      final resp = await http
          .get(_uri('/boot_log', {'port': port, 'window_s': windowS.toString()}))
          .timeout(Duration(seconds: windowS.toInt() + 15));
      return jsonDecode(resp.body) as Map<String, dynamic>;
    } catch (e) {
      return {'ok': false, 'passed': false, 'error': 'Could not reach Flash Bridge at $host ($e)'};
    }
  }

  Future<bool> logResult(Map<String, dynamic> report) async {
    try {
      final resp = await http
          .post(_uri('/log_result'), headers: {'Content-Type': 'application/json'}, body: jsonEncode(report))
          .timeout(const Duration(seconds: 5));
      return resp.statusCode == 200;
    } catch (_) {
      return false;
    }
  }

  Future<List<Map<String, dynamic>>> results({int limit = 50}) async {
    try {
      final resp = await http.get(_uri('/results', {'limit': limit.toString()})).timeout(const Duration(seconds: 5));
      final body = jsonDecode(resp.body) as Map<String, dynamic>;
      return List<Map<String, dynamic>>.from(body['rows'] as List);
    } catch (_) {
      return [];
    }
  }
}
