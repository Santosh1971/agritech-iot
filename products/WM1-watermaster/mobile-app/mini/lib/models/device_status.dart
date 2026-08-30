enum SchedulerState { idle, running, paused, queuedWaiting }

SchedulerState schedulerStateFromInt(int v) => switch (v) {
      1 => SchedulerState.running,
      2 => SchedulerState.paused,
      3 => SchedulerState.queuedWaiting,
      _ => SchedulerState.idle,
    };

/// Custom display labels only — relay ROLES stay fixed (pump/dosing/4
/// valves), see firmware's RelayNames.h. Renaming never changes what a
/// relay does, just what it's called everywhere in this app.
class RelayNames {
  final String pump;
  final String dosing;
  final List<String> valves;

  const RelayNames({
    this.pump = 'Pump',
    this.dosing = 'Dosing',
    this.valves = const ['Valve 1', 'Valve 2', 'Valve 3', 'Valve 4'],
  });

  String valveName(int i) => (i >= 0 && i < valves.length) ? valves[i] : 'Valve ${i + 1}';

  factory RelayNames.fromJson(Map<String, dynamic>? j) {
    if (j == null) return const RelayNames();
    return RelayNames(
      pump: j['pump'] as String? ?? 'Pump',
      dosing: j['dosing'] as String? ?? 'Dosing',
      valves: (j['valves'] as List?)?.map((e) => e as String).toList() ??
          const ['Valve 1', 'Valve 2', 'Valve 3', 'Valve 4'],
    );
  }
}

class DeviceStatus {
  final String deviceId;
  final SchedulerState state;
  final bool pump;
  final bool dosing;
  final List<bool> valves; // index 0..3 = valve 1..4 (RL3-RL6)
  final bool wifiConnected;
  final int wifiRssi;
  final bool forcedLocal;
  final int programsCount;
  final String rtcDate;
  final String rtcTime;
  final bool rtcOk;
  final RelayNames relayNames;

  // Real ADC/pulse reads — see firmware's Sensors.h for the documented
  // placeholder scale factors (no real 4-20mA sensor/flow meter/level
  // switch wired up yet, so these are plumbing-verified, not calibrated).
  final double pressure1Bar;
  final double pressure2Bar;
  final double flowRateLpm;
  final double flowTotalLiters;
  final bool waterLevelOk;
  final double batteryVolts;

  // Only present while a sequence is active (RUNNING or PAUSED).
  final int? activeProgramId;
  final String? activeProgramName;
  final String? activeSequenceName;
  final int? activeSeqIndex;
  final int? elapsedSec;
  final int? runTargetSec;

  const DeviceStatus({
    required this.deviceId,
    required this.state,
    required this.pump,
    required this.dosing,
    required this.valves,
    required this.wifiConnected,
    required this.wifiRssi,
    required this.forcedLocal,
    required this.programsCount,
    this.rtcDate = '--/--/----',
    this.rtcTime = '--:--',
    this.rtcOk = false,
    this.relayNames = const RelayNames(),
    this.pressure1Bar = 0,
    this.pressure2Bar = 0,
    this.flowRateLpm = 0,
    this.flowTotalLiters = 0,
    this.waterLevelOk = true,
    this.batteryVolts = 0,
    this.activeProgramId,
    this.activeProgramName,
    this.activeSequenceName,
    this.activeSeqIndex,
    this.elapsedSec,
    this.runTargetSec,
  });

  factory DeviceStatus.empty() => const DeviceStatus(
        deviceId: '',
        state: SchedulerState.idle,
        pump: false,
        dosing: false,
        valves: [false, false, false, false],
        wifiConnected: false,
        wifiRssi: 0,
        forcedLocal: false,
        programsCount: 0,
      );

  factory DeviceStatus.fromJson(Map<String, dynamic> j) => DeviceStatus(
        deviceId: j['device_id'] as String? ?? '',
        state: schedulerStateFromInt((j['state'] as num?)?.toInt() ?? 0),
        pump: j['pump'] as bool? ?? false,
        dosing: j['dosing'] as bool? ?? false,
        valves: (j['valves'] as List?)?.map((e) => e as bool).toList() ??
            const [false, false, false, false],
        wifiConnected: j['wifi_connected'] as bool? ?? false,
        wifiRssi: (j['wifi_rssi'] as num?)?.toInt() ?? 0,
        forcedLocal: j['forced_local'] as bool? ?? false,
        programsCount: (j['programs_count'] as num?)?.toInt() ?? 0,
        rtcDate: j['rtc_date'] as String? ?? '--/--/----',
        rtcTime: j['rtc_time'] as String? ?? '--:--',
        rtcOk: j['rtc_ok'] as bool? ?? false,
        relayNames: RelayNames.fromJson(j['relay_names'] as Map<String, dynamic>?),
        pressure1Bar: (j['pressure1_bar'] as num?)?.toDouble() ?? 0,
        pressure2Bar: (j['pressure2_bar'] as num?)?.toDouble() ?? 0,
        flowRateLpm: (j['flow_rate_lpm'] as num?)?.toDouble() ?? 0,
        flowTotalLiters: (j['flow_total_liters'] as num?)?.toDouble() ?? 0,
        waterLevelOk: j['water_level_ok'] as bool? ?? true,
        batteryVolts: (j['battery_volts'] as num?)?.toDouble() ?? 0,
        activeProgramId: (j['active_program_id'] as num?)?.toInt(),
        activeProgramName: j['active_program_name'] as String?,
        activeSequenceName: j['active_sequence_name'] as String?,
        activeSeqIndex: (j['active_seq_index'] as num?)?.toInt(),
        elapsedSec: (j['elapsed_sec'] as num?)?.toInt(),
        runTargetSec: (j['run_target_sec'] as num?)?.toInt(),
      );
}
