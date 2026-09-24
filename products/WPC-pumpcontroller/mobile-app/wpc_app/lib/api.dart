import 'dart:convert';
import 'package:http/http.dart' as http;
import 'backend.dart';

/// The one place the screens talk to a WPC device.
///
/// Master calls (status, levels, override, config...) go over local HTTP or
/// through the cloud depending on [Backend.mode]; the screens don't care which.
/// Anything that only makes sense next to the hardware (a Pump Node's own
/// SoftAP, the Master's WiFi credentials) is local-only and says so.
class WpcApi {
  // Same gateway IP whether the phone is on the Master's or a Pump's
  // SoftAP -- only one is connected at a time, so this is safe to share.
  static const String baseUrl = 'http://192.168.4.1';
  static const Duration _timeout = Duration(seconds: 5);

  static bool get _cloud => Backend.instance.mode == LinkMode.cloud;

  // ------------------------------------------------------------ local plumbing
  static Future<void> _localPost(String path, Map<String, dynamic> body,
      {Duration? timeout}) async {
    final res = await http
        .post(
          Uri.parse('$baseUrl$path'),
          headers: {'Content-Type': 'application/json'},
          body: jsonEncode(body),
        )
        .timeout(timeout ?? _timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
  }

  /// A Master command: HTTP `path` locally, MQTT `cloudCmd` remotely. Both end
  /// up in the same handler on the Master.
  static Future<void> _masterCmd(
      String path, Map<String, dynamic> body, Map<String, dynamic> cloudCmd) {
    if (_cloud) return Backend.instance.sendCloudCommand(cloudCmd);
    return _localPost(path, body);
  }

  // ------------------------------------------------------------------- Pump Node
  // Direct connection to a Pump's own SoftAP -- never goes through the cloud.
  static Future<Map<String, dynamic>> getPumpInfo() async {
    final res = await http.get(Uri.parse('$baseUrl/info')).timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  static Future<void> setPumpConfig({int? pumpId, String? targetMasterId}) {
    final body = <String, dynamic>{};
    if (pumpId != null) body['pumpId'] = pumpId;
    if (targetMasterId != null) body['targetMasterId'] = targetMasterId;
    return _localPost('/config', body);
  }

  /// TX power of the Pump Node you are connected to (its own radio only).
  static Future<void> setPumpTxPower(int dbm) => _localPost('/config', {'txPower': dbm});

  /// Makes the Pump you're connected to forget its target Master: it stops trying to join
  /// anyone (no more JOIN_REQUEST airtime) and its relay stays fail-safe OFF until it's pointed
  /// at a Master again. This is the Pump-side half of disassociating a pump -- the Master's own
  /// [forgetPump] only removes it from that Master's table and doesn't touch the Pump, so a pump
  /// unpaired only there still had the old Master's ID saved and would rejoin it on its own.
  static Future<void> forgetPumpMaster() => _localPost('/forget', {});

  // ---------------------------------------------------------------------- Master
  static Future<Map<String, dynamic>> getLocalStatus() async {
    final res = await http.get(Uri.parse('$baseUrl/status')).timeout(_timeout);
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  static Future<Map<String, dynamic>> getStatus() {
    if (_cloud) return Backend.instance.cloudStatus();
    return getLocalStatus();
  }

  static Future<void> setDebounceMs(int ms) =>
      _masterCmd('/config', {'debounceMs': ms}, {'cmd': 'set_config', 'debounceMs': ms});

  // dBm, valid range -9..22 on the SX1262 -- higher trades battery/duty-cycle
  // headroom for range. Each node's TX power only affects what THAT radio
  // transmits, so Master and Pump must each be set independently for a
  // link's range to change in both directions.
  static Future<void> setTxPower(int dbm) =>
      _masterCmd('/config', {'txPower': dbm}, {'cmd': 'set_config', 'txPower': dbm});

  static Future<void> setNumLevels(int n) =>
      _masterCmd('/config', {'numLevels': n}, {'cmd': 'set_config', 'numLevels': n});

  static Future<void> forgetPump(int slot) =>
      _masterCmd('/forget', {'slot': slot}, {'cmd': 'forget', 'slot': slot});

  static Future<void> setPumpName(int slot, String name) => _masterCmd(
      '/name', {'slot': slot, 'name': name}, {'cmd': 'set_name', 'slot': slot, 'name': name});

  static Future<void> setPumpLevel(int slot, int level, bool assigned) => _masterCmd(
      '/assign',
      {'slot': slot, 'level': level, 'assigned': assigned},
      {'cmd': 'assign', 'slot': slot, 'level': level, 'assigned': assigned});

  // enabled=false switches the pump back to automatic (level-logic) control.
  // enabled=true with state set forces the relay to that state until
  // overridden again or disabled.
  static Future<void> setPumpOverride(int slot, bool enabled, {bool? state}) {
    final body = <String, dynamic>{'slot': slot, 'enabled': enabled};
    if (state != null) body['state'] = state;
    return _masterCmd('/override', body, {'cmd': 'override', ...body});
  }

  /// Asks the Master to scan for WiFi networks. Non-blocking on the Master: returns at once,
  /// then poll [getWifiScan] until it reports `scanning: false`. LOCAL ONLY.
  static Future<void> startWifiScan() =>
      _localPost('/wifi/scan', {}, timeout: const Duration(seconds: 8));

  /// `{"scanning": bool, "networks": [{ssid, rssi, open}, ...]}` -- strongest first, one entry per
  /// name. While `scanning` is true the list is the previous scan's, so wait for false.
  static Future<Map<String, dynamic>> getWifiScan() async {
    final res = await http.get(Uri.parse('$baseUrl/wifi/scan')).timeout(const Duration(seconds: 8));
    if (res.statusCode != 200) throw Exception('HTTP ${res.statusCode}');
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  /// Gives the Master the farm WiFi it should use for internet. LOCAL ONLY --
  /// the Master refuses this over the cloud on purpose, so nobody can knock a
  /// remote installation offline. The Master may briefly drop its own access
  /// point while it joins the router, so a timeout here is not necessarily a
  /// failure; check the Master's WiFi status afterwards.
  static Future<void> setMasterWifi(String ssid, String password) =>
      _localPost('/wifi', {'ssid': ssid, 'password': password},
          timeout: const Duration(seconds: 12));
}
