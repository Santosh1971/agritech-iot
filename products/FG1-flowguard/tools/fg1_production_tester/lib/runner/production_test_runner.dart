import 'dart:async';
import 'package:flutter/foundation.dart';
import '../models/test_report.dart';
import '../providers/bench_config.dart';
import '../services/dut_service.dart';
import '../services/flash_bridge_service.dart';
import '../services/jig_service.dart';
import '../services/report_store.dart';

/// Where the current unit is in the session flow -- see
/// docs/testing/PRODUCTION_TOOL_SPEC.md section 5 for the full table
/// this state machine implements.
/// Matches platformio.ini's own hardcoded `monitor_port`/`upload_port`
/// -- that file's comment explains why this bench assumes one fixed
/// port rather than auto-detecting. Flashing can omit the port (pio
/// falls back to platformio.ini itself), but boot-log capture always
/// needs an explicit port to open, so this is the fallback when the
/// operator leaves the field blank.
const String kDefaultDutPort = '/dev/cu.usbserial-0001';

enum RunPhase {
  idle,
  flashing,
  bootCheck,
  awaitingWifiJoin,
  runningTests,
  done,
}

/// Orchestrates one unit's full test session -- a direct port of
/// testing/test_production.py's run() function, split across the
/// Bench-LAN phase (flash + boot log, via FlashBridgeService) and the
/// DUT-SoftAP phase (everything else, via DutService + JigService).
class ProductionTestRunner extends ChangeNotifier {
  final BenchConfig config;
  final ReportStore reportStore;

  ProductionTestRunner({required this.config, required this.reportStore});

  late final FlashBridgeService flashBridge = FlashBridgeService(host: config.flashBridgeHost);
  late final JigService jig = JigService(host: config.jigHost);
  DutService? dut;

  RunPhase phase = RunPhase.idle;
  String? deviceId;
  bool jigAvailable = false;
  final List<String> flashLog = [];

  /// Insertion-ordered so the UI can render steps top-to-bottom as
  /// they complete, matching kProductionTestSteps order.
  final Map<String, StepResult> steps = {
    for (final name in kProductionTestSteps) name: StepResult(name),
  };

  void _setStep(String name, bool passed, {String detail = ''}) {
    steps[name] = StepResult(name, passed: passed, detail: detail);
    notifyListeners();
  }

  void reset() {
    phase = RunPhase.idle;
    deviceId = null;
    jigAvailable = false;
    flashLog.clear();
    dut?.close();
    dut = null;
    for (final name in kProductionTestSteps) {
      steps[name] = StepResult(name);
    }
    notifyListeners();
  }

  // ---------------- Phase 1: Bench LAN (flash + boot log) ----------------

  Future<void> startFlash({String? dutPort}) async {
    reset();
    phase = RunPhase.flashing;
    notifyListeners();

    bool flashOk = false;
    await for (final event in flashBridge.flash(env: config.firmwareEnv, port: dutPort)) {
      if (event['type'] == 'log') {
        flashLog.add(event['line'] as String);
        notifyListeners();
      } else if (event['type'] == 'result') {
        flashOk = event['passed'] == true;
      }
    }
    _setStep('flash', flashOk, detail: flashOk ? '' : 'see flash log');
    if (!flashOk) {
      phase = RunPhase.done;
      await _saveReport();
      notifyListeners();
      return;
    }

    phase = RunPhase.bootCheck;
    notifyListeners();
    // Give the DUT a moment after upload finishes before opening the
    // monitor port -- matches serial_monitor.py's own reset handling.
    await Future.delayed(const Duration(seconds: 2));
    final boot = await flashBridge.bootLog(
        port: (dutPort == null || dutPort.isEmpty) ? kDefaultDutPort : dutPort, windowS: 10);
    final bootOk = boot['ok'] == true && boot['passed'] == true;
    deviceId = boot['device_id'] as String?;
    _setStep('boot_log', bootOk, detail: (boot['error'] as String?) ?? deviceId ?? 'no device id found');

    if (!bootOk) {
      phase = RunPhase.done;
      await _saveReport();
      notifyListeners();
      return;
    }

    phase = RunPhase.awaitingWifiJoin;
    notifyListeners();
  }

  // ---------------- Phase 2: DUT's own SoftAP ----------------

  /// Call once the operator has manually joined the DUT's own WiFi
  /// network (the app can only prompt for this -- see
  /// PRODUCTION_TOOL_SPEC.md section 8.2 step 3 and the reasoning in
  /// local_setup_screen.dart's header comment about why this can't be
  /// automated on either platform).
  Future<void> confirmDeviceWifiJoined() async {
    phase = RunPhase.runningTests;
    notifyListeners();

    dut = DutService();
    final connected = await dut!.connect();
    if (!connected) {
      _setStep('factory_reset', false, detail: 'could not connect to DUT WS API');
      phase = RunPhase.done;
      await _saveReport();
      notifyListeners();
      return;
    }

    jigAvailable = await jig.ping();
    notifyListeners();

    try {
      await _runFunctionalTests();
    } catch (e) {
      // Safety net -- an unexpected exception here (e.g. a dropped
      // socket at an unlucky moment) would otherwise leave the UI
      // stuck on "Running tests" forever with no way forward.
      final firstUnset = steps.values.firstWhere((s) => s.passed == null, orElse: () => steps.values.last);
      _setStep(firstUnset.name, false, detail: 'unexpected error: $e');
    }

    phase = RunPhase.done;
    await _saveReport();
    notifyListeners();
  }

  Future<void> _runFunctionalTests() async {
    // 3. Factory reset
    await dut!.sendCommand('factory_reset', timeout: const Duration(seconds: 3));
    await Future.delayed(const Duration(seconds: 3)); // DUT reboots
    dut!.close();
    final reconnected = await dut!.connect();
    _setStep('factory_reset', reconnected);
    if (!reconnected) return;

    // 4. Calibrate
    final calResp = await dut!.sendCommand('calibrate', extra: {'ppl': config.expectedCalibrationPpl});
    _setStep('calibrate', calResp != null);

    // 5. Relay test
    await dut!.sendCommand('relay_test', timeout: const Duration(seconds: 3));
    await Future.delayed(const Duration(milliseconds: 800));
    if (jigAvailable) {
      final relayOn = await jig.relayState();
      _setStep('relay_test', relayOn == true,
          detail: relayOn == true ? 'jig sensed relay ON' : 'jig did NOT sense relay closing');
    } else {
      _setStep('relay_test', true, detail: 'jig not connected -- unverified (firmware self-report only)');
    }

    // 6. Flow sensor
    if (jigAvailable) {
      final before = (await dut!.deviceInfo())?['liters_delivered'] as num? ?? 0;
      await jig.pulse(config.flowTestPulseCount);
      await Future.delayed(const Duration(milliseconds: 500));
      final after = (await dut!.deviceInfo())?['liters_delivered'] as num? ?? 0;
      final delivered = after.toDouble() - before.toDouble();
      final expected = config.flowTestPulseCount / config.expectedCalibrationPpl;
      final withinTolerance = (delivered - expected).abs() <= expected * 0.02;
      _setStep('flow_sensor', withinTolerance,
          detail: 'expected ${expected.toStringAsFixed(2)}L, got ${delivered.toStringAsFixed(2)}L');
    } else {
      _setStep('flow_sensor', true, detail: 'jig not connected -- unverified');
    }

    // 7. WiFi + MQTT
    await dut!.sendCommand('wifi_config', extra: {
      'ssid': config.testApSsid,
      'pass': config.testApPassword,
    });
    await dut!.sendCommand('resume_auto_mode', timeout: const Duration(seconds: 3));
    final info = await dut!.waitFor(
      (i) => i['wifi_connected'] == true && i['mqtt_connected'] == true,
      timeout: const Duration(seconds: 25),
    );
    _setStep('wifi_mqtt', info != null, detail: info == null ? 'timed out waiting for wifi+mqtt' : '');

    // 8. RTC sanity
    final rtcInfo = await dut!.deviceInfo();
    _setStep('rtc', rtcInfo?['rtc_set'] == true, detail: 'rtc_time=${rtcInfo?['rtc_time']}');

    // 9. Ship-clean reset -- unlike test_production.py's unconditional
    // `step("ship_clean_reset", True)`, this checks the ack actually
    // came back: sendCommand's auto-reconnect (see DutService) means a
    // send failure here is a real signal, not just socket noise from
    // the wifi_mqtt step's reconnect a moment ago.
    final resetAck = await dut!.sendCommand('factory_reset', timeout: const Duration(seconds: 5));
    _setStep('ship_clean_reset', resetAck != null,
        detail: resetAck == null ? 'no ack -- unit may still carry bench WiFi/MQTT config' : '');
  }

  Future<TestReport> _saveReport() async {
    final report = TestReport(
      deviceId: deviceId,
      firmwareEnv: config.firmwareEnv,
      timestampUtc: DateTime.now().toUtc(),
      operator: config.operatorName,
      station: config.stationName,
      steps: Map.of(steps),
    );
    await reportStore.add(report);
    // Best-effort mirror onto the laptop's CSV too -- fine if this
    // fails (e.g. phone already switched off the Bench LAN).
    unawaited(flashBridge.logResult(report.toJson()));
    return report;
  }

  TestReport buildReport() => TestReport(
        deviceId: deviceId,
        firmwareEnv: config.firmwareEnv,
        timestampUtc: DateTime.now().toUtc(),
        operator: config.operatorName,
        station: config.stationName,
        steps: Map.of(steps),
      );
}
