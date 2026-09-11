/// One test step's outcome -- mirrors the `steps` dict test_production.py
/// builds up (testing/test_production.py), just typed instead of a loose
/// dict, and with a `detail` string for whatever's worth showing the
/// operator (e.g. "expected 1.00L, got 0.99L").
class StepResult {
  final String name;
  final bool? passed; // null = not yet run
  final String detail;

  const StepResult(this.name, {this.passed, this.detail = ''});

  StepResult copyWith({bool? passed, String? detail}) =>
      StepResult(name, passed: passed ?? this.passed, detail: detail ?? this.detail);

  Map<String, dynamic> toJson() => {'passed': passed, 'detail': detail};
}

/// The full record for one unit's test run -- this is what gets stored
/// locally, exported to CSV, and (optionally) POSTed to the Flash
/// Bridge's /log_result so the laptop's CSV has a copy too.
///
/// Field names deliberately match docs/testing/PRODUCTION_TOOL_SPEC.md
/// section 8.4's JSON schema.
class TestReport {
  final String? deviceId;
  final String firmwareEnv;
  final DateTime timestampUtc;
  final String operator;
  final String station;
  final Map<String, StepResult> steps;

  TestReport({
    required this.deviceId,
    required this.firmwareEnv,
    required this.timestampUtc,
    required this.operator,
    required this.station,
    required this.steps,
  });

  bool get overallPassed =>
      steps.isNotEmpty && steps.values.every((s) => s.passed == true);

  bool get hasFailed => steps.values.any((s) => s.passed == false);

  Map<String, dynamic> toJson() => {
        'device_id': deviceId,
        'firmware_env': firmwareEnv,
        'timestamp_utc': timestampUtc.toIso8601String(),
        'operator': operator,
        'station': station,
        'steps': steps.map((k, v) => MapEntry(k, v.toJson())),
        'overall_passed': overallPassed,
      };

  factory TestReport.fromJson(Map<String, dynamic> json) => TestReport(
        deviceId: json['device_id'] as String?,
        firmwareEnv: json['firmware_env'] as String? ?? 'esp32dev_ds1307',
        timestampUtc: DateTime.parse(json['timestamp_utc'] as String),
        operator: json['operator'] as String? ?? '',
        station: json['station'] as String? ?? '',
        steps: (json['steps'] as Map<String, dynamic>? ?? {}).map(
          (k, v) => MapEntry(
            k,
            StepResult(k,
                passed: v['passed'] as bool?, detail: v['detail'] as String? ?? ''),
          ),
        ),
      );

  /// Row shape matching testing/results_logger.py's CSV columns, so
  /// this app's export and the old Python tool's log can be merged.
  Map<String, String> toCsvRow() => {
        'timestamp_utc': timestampUtc.toIso8601String(),
        'device_id': deviceId ?? 'UNKNOWN',
        'tier': 'production',
        'passed': overallPassed.toString(),
        'steps_json': _stepsJsonString(),
      };

  String _stepsJsonString() {
    final entries = steps.entries
        .map((e) => '"${e.key}":${e.value.passed == true}')
        .join(',');
    return '{$entries}';
  }
}

/// The fixed order of Production Test (Tier 2) steps -- see
/// docs/testing/TEST_JIG_SPEC.md section 6.
const List<String> kProductionTestSteps = [
  'flash',
  'boot_log',
  'factory_reset',
  'calibrate',
  'relay_test',
  'flow_sensor',
  'wifi_mqtt',
  'rtc',
  'ship_clean_reset',
];

const Map<String, String> kStepLabels = {
  'flash': 'Flash firmware',
  'boot_log': 'Boot log check',
  'factory_reset': 'Factory reset',
  'calibrate': 'Set calibration',
  'relay_test': 'Relay physical test',
  'flow_sensor': 'Flow sensor accuracy',
  'wifi_mqtt': 'WiFi + MQTT connect',
  'rtc': 'RTC sanity',
  'ship_clean_reset': 'Ship-clean reset',
};
