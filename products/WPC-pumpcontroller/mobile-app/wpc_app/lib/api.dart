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

  static Future<void> setDebounceMs(int ms) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/config'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'debounceMs': ms}),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }

  // dBm, valid range -9..22 on the SX1262 -- higher trades battery/duty-cycle
  // headroom for range. Each node's TX power only affects what THAT radio
  // transmits, so Master and Pump must each be set independently for a
  // link's range to change in both directions.
  static Future<void> setTxPower(int dbm) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/config'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'txPower': dbm}),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
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

  static Future<void> forgetPump(int slot) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl/forget'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode({'slot': slot}),
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

  // enabled=false switches the pump back to automatic (level-logic) control.
  // enabled=true with state set forces the relay to that state until
  // overridden again or disabled.
  static Future<void> setPumpOverride(int slot, bool enabled, {bool? state}) async {
    final body = <String, dynamic>{'slot': slot, 'enabled': enabled};
    if (state != null) body['state'] = state;
    final res = await http
        .post(
          Uri.parse('$baseUrl/override'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(body),
        )
        .timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }
}
