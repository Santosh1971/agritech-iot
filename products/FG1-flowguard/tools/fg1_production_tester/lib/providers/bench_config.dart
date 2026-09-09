import 'package:shared_preferences/shared_preferences.dart';

/// Bench-station settings -- set once per bench, persisted locally.
/// Mirrors the constants at the top of testing/test_production.py.
class BenchConfig {
  final String flashBridgeHost; // e.g. "192.168.1.50:8787"
  final String jigHost; // e.g. "fg1jig.local"
  final String firmwareEnv; // "esp32dev" or "esp32dev_ds1307"
  final String testApSsid; // Bench LAN, see TEST_JIG_SPEC.md section 2.5
  final String testApPassword;
  final int expectedCalibrationPpl;
  final int flowTestPulseCount;
  final String operatorName;
  final String stationName;

  const BenchConfig({
    this.flashBridgeHost = '',
    // Static IP the jig configures itself once it joins the DUT's
    // SoftAP -- not fg1jig.local, since stock Android's plain HTTP
    // client doesn't resolve mDNS hostnames (see jig firmware's
    // header comment). mDNS is still up for laptop-side debugging.
    this.jigHost = '192.168.4.50',
    this.firmwareEnv = 'esp32dev',
    this.testApSsid = 'FG1-TEST-STATION',
    this.testApPassword = '',
    this.expectedCalibrationPpl = 450,
    this.flowTestPulseCount = 450,
    this.operatorName = '',
    this.stationName = 'bench-1',
  });

  BenchConfig copyWith({
    String? flashBridgeHost,
    String? jigHost,
    String? firmwareEnv,
    String? testApSsid,
    String? testApPassword,
    int? expectedCalibrationPpl,
    int? flowTestPulseCount,
    String? operatorName,
    String? stationName,
  }) =>
      BenchConfig(
        flashBridgeHost: flashBridgeHost ?? this.flashBridgeHost,
        jigHost: jigHost ?? this.jigHost,
        firmwareEnv: firmwareEnv ?? this.firmwareEnv,
        testApSsid: testApSsid ?? this.testApSsid,
        testApPassword: testApPassword ?? this.testApPassword,
        expectedCalibrationPpl: expectedCalibrationPpl ?? this.expectedCalibrationPpl,
        flowTestPulseCount: flowTestPulseCount ?? this.flowTestPulseCount,
        operatorName: operatorName ?? this.operatorName,
        stationName: stationName ?? this.stationName,
      );

  static const _prefsPrefix = 'fg1_bench_config_';

  Future<void> save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('${_prefsPrefix}flash_bridge_host', flashBridgeHost);
    await prefs.setString('${_prefsPrefix}jig_host', jigHost);
    await prefs.setString('${_prefsPrefix}firmware_env', firmwareEnv);
    await prefs.setString('${_prefsPrefix}test_ap_ssid', testApSsid);
    await prefs.setString('${_prefsPrefix}test_ap_password', testApPassword);
    await prefs.setInt('${_prefsPrefix}expected_ppl', expectedCalibrationPpl);
    await prefs.setInt('${_prefsPrefix}flow_pulse_count', flowTestPulseCount);
    await prefs.setString('${_prefsPrefix}operator', operatorName);
    await prefs.setString('${_prefsPrefix}station', stationName);
  }

  static Future<BenchConfig> load() async {
    final prefs = await SharedPreferences.getInstance();
    const d = BenchConfig();
    return BenchConfig(
      flashBridgeHost: prefs.getString('${_prefsPrefix}flash_bridge_host') ?? d.flashBridgeHost,
      jigHost: prefs.getString('${_prefsPrefix}jig_host') ?? d.jigHost,
      firmwareEnv: prefs.getString('${_prefsPrefix}firmware_env') ?? d.firmwareEnv,
      testApSsid: prefs.getString('${_prefsPrefix}test_ap_ssid') ?? d.testApSsid,
      testApPassword: prefs.getString('${_prefsPrefix}test_ap_password') ?? d.testApPassword,
      expectedCalibrationPpl: prefs.getInt('${_prefsPrefix}expected_ppl') ?? d.expectedCalibrationPpl,
      flowTestPulseCount: prefs.getInt('${_prefsPrefix}flow_pulse_count') ?? d.flowTestPulseCount,
      operatorName: prefs.getString('${_prefsPrefix}operator') ?? d.operatorName,
      stationName: prefs.getString('${_prefsPrefix}station') ?? d.stationName,
    );
  }
}
