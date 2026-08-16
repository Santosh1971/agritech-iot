import 'dart:convert';
import 'package:http/http.dart' as http;

class WpcApi {
  static const String baseUrl = 'http://192.168.4.1';
  static const Duration _timeout = Duration(seconds: 5);

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
