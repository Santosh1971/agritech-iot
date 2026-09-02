enum SchedulerState { idle, running, paused, queuedWaiting }

SchedulerState schedulerStateFromInt(int v) => switch (v) {
      1 => SchedulerState.running,
      2 => SchedulerState.paused,
      3 => SchedulerState.queuedWaiting,
      _ => SchedulerState.idle,
    };

/// Custom display labels only — hardware ROLES stay fixed (pump/dosing/4
/// valves/2 pressure sensors/flow meter/2 level switches), see firmware's
/// RelayNames.h. Renaming never changes what a relay or input does, just
/// what it's called. Each field here is the bare name the user typed
/// ("Mirchi") — the fixed hardware suffix ("_R3") is appended by the
/// display getters below, never stored, so it can't go stale if the
/// suffix convention ever changes.
class RelayNames {
  final String pump;
  final String dosing;
  final List<String> valves;
  final String pressure1;
  final String pressure2;
  final String flow;
  final String waterUpper; // L1 / IN1
  final String waterLower; // L2 / IN2

  const RelayNames({
    this.pump = 'Pump',
    this.dosing = 'Dosing',
    this.valves = const ['Valve 1', 'Valve 2', 'Valve 3', 'Valve 4'],
    this.pressure1 = 'Pressure 1',
    this.pressure2 = 'Pressure 2',
    this.flow = 'Water Meter',
    this.waterUpper = 'Upper',
    this.waterLower = 'Lower',
  });

  String valveName(int i) => (i >= 0 && i < valves.length) ? valves[i] : 'Valve ${i + 1}';

  // Display forms with the fixed hardware suffix appended — use these
  // (not the bare fields above) anywhere the name is shown to the user,
  // so it always reads e.g. "Mirchi_R3" and can't be mistaken for a
  // different physical point.
  String get pumpDisplay => '${pump}_R1';
  String get dosingDisplay => '${dosing}_R2';
  String valveDisplay(int i) => '${valveName(i)}_R${i + 3}';
  String get pressure1Display => '${pressure1}_P1';
  String get pressure2Display => '${pressure2}_P2';
  String get flowDisplay => '${flow}_FL';
  String get waterUpperDisplay => '${waterUpper}_IN1';
  String get waterLowerDisplay => '${waterLower}_IN2';

  factory RelayNames.fromJson(Map<String, dynamic>? j) {
    if (j == null) return const RelayNames();
    return RelayNames(
      pump: j['pump'] as String? ?? 'Pump',
      dosing: j['dosing'] as String? ?? 'Dosing',
      valves: (j['valves'] as List?)?.map((e) => e as String).toList() ??
          const ['Valve 1', 'Valve 2', 'Valve 3', 'Valve 4'],
      pressure1: j['pressure1'] as String? ?? 'Pressure 1',
      pressure2: j['pressure2'] as String? ?? 'Pressure 2',
      flow: j['flow'] as String? ?? 'Water Meter',
      waterUpper: j['waterUpper'] as String? ?? 'Upper',
      waterLower: j['waterLower'] as String? ?? 'Lower',
    );
  }
}

class DeviceStatus {
  final String deviceId;
  final SchedulerState state;
  // "water" | "power" | "manual" | "" — empty unless state is paused.
  final String pauseReason;
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
  final double flowPulsesPerLiter;
  final int flowTotalPulsesRaw;
  final bool waterLevelEnabled;
  final bool waterL1Ok;
  final bool waterL2Ok;
  final bool waterLevelOk;
  final double batteryVolts;

  // Only present while a sequence is active (RUNNING or PAUSED).
  final int? activeProgramId;
  final String? activeProgramName;
  final String? activeSequenceName;
  final int? activeSeqIndex;
  final int? elapsedSec;
  final int? runTargetSec;
  // "time" | "volume" — which of the pairs above/below is the one that
  // actually determines completion for the active sequence.
  final String? runMode;
  final int? runTargetLiters;
  final double? elapsedLiters;

  // The soonest future auto-fire across all enabled+autoStart programs
  // — computed device-side (see Scheduler::computeNextRun) since the
  // app never sees the internal per-day tracking needed to know
  // whether an interval-days program is actually due today or not.
  // Absent when nothing is scheduled to run automatically at all.
  final int? nextRunProgramId;
  final String? nextRunProgramName;
  final int? nextRunEpoch; // unix seconds, same wall-clock-as-epoch convention as rtc_sync

  const DeviceStatus({
    required this.deviceId,
    required this.state,
    this.pauseReason = '',
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
    this.flowPulsesPerLiter = 5.5,
    this.flowTotalPulsesRaw = 0,
    this.waterLevelEnabled = false,
    this.waterL1Ok = true,
    this.waterL2Ok = true,
    this.waterLevelOk = true,
    this.batteryVolts = 0,
    this.activeProgramId,
    this.activeProgramName,
    this.activeSequenceName,
    this.activeSeqIndex,
    this.elapsedSec,
    this.runTargetSec,
    this.runMode,
    this.runTargetLiters,
    this.elapsedLiters,
    this.nextRunProgramId,
    this.nextRunProgramName,
    this.nextRunEpoch,
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
        pauseReason: j['pause_reason'] as String? ?? '',
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
        flowPulsesPerLiter: (j['flow_pulses_per_liter'] as num?)?.toDouble() ?? 5.5,
        flowTotalPulsesRaw: (j['flow_total_pulses_raw'] as num?)?.toInt() ?? 0,
        waterLevelEnabled: j['water_level_enabled'] as bool? ?? false,
        waterL1Ok: j['water_l1_ok'] as bool? ?? true,
        waterL2Ok: j['water_l2_ok'] as bool? ?? true,
        waterLevelOk: j['water_level_ok'] as bool? ?? true,
        batteryVolts: (j['battery_volts'] as num?)?.toDouble() ?? 0,
        activeProgramId: (j['active_program_id'] as num?)?.toInt(),
        activeProgramName: j['active_program_name'] as String?,
        activeSequenceName: j['active_sequence_name'] as String?,
        activeSeqIndex: (j['active_seq_index'] as num?)?.toInt(),
        elapsedSec: (j['elapsed_sec'] as num?)?.toInt(),
        runTargetSec: (j['run_target_sec'] as num?)?.toInt(),
        runMode: j['run_mode'] as String?,
        runTargetLiters: (j['run_target_liters'] as num?)?.toInt(),
        elapsedLiters: (j['elapsed_liters'] as num?)?.toDouble(),
        nextRunProgramId: (j['next_run_program_id'] as num?)?.toInt(),
        nextRunProgramName: j['next_run_program_name'] as String?,
        nextRunEpoch: (j['next_run_epoch'] as num?)?.toInt(),
      );
}
