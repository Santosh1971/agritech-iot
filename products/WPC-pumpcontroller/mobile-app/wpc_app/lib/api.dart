import 'dart:convert';
import 'package:http/http.dart' as http;

class WpcApi {
  // Same gateway IP whether the phone is on the Master's or a Pump's
  // SoftAP -- only one is connected at a time, so this is safe to share.
  static const String baseUrl = 'http://192.168.4.1';
  static const Duration _timeout = Duration(seconds: 5);

  static Future<Map<String, dynamic>> getPumpInfo() async {
    final res = await http.get(Uri.parse('$baseUrl/info')).timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  static Future<void> setPumpConfig({int? pumpId, String? targetMasterId}) async {
    final body = <String, dynamic>{};
    if (pumpId != null) body['pumpId'] = pumpId;
    if (targetMasterId != null) body['targetMasterId'] = targetMasterId;
    final res = await http
        .post(
          Uri.parse('$baseUrl/config'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(body),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }

  static Future<Map<String, dynamic>> getStatus() async {
    final res = await http.get(Uri.parse('$baseUrl/status')).timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  static Future<void> setNumLevels(int n) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/config'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'numLevels': n}),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }

  static Future<void> setPumpName(int slot, String name) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/name'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'slot': slot, 'name': name}),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }

  static Future<void> setPumpLevel(int slot, int level, bool assigned) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/assign'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'slot': slot, 'level': level, 'assigned': assigned}),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }
}
