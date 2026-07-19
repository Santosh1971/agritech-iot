class DeviceStatus {
  final String deviceId;
  final String firmware;
  final bool pumpOn;
  final String rtcTime;
  final String rtcDate;
  final bool rtcSet;
  final int wifiRssi;
  final bool wifiConnected;
  final bool mqttConnected;
  final bool cycleActive;
  final bool cyclePaused;
  final int cycleId;
  final double litersDelivered;
  final String startedBy;
  final int cycleStartUnix; // 0 if no cycle active/ever run

  DeviceStatus({
    required this.deviceId,
    required this.firmware,
    required this.pumpOn,
    required this.rtcTime,
    required this.rtcDate,
    required this.rtcSet,
    required this.wifiRssi,
    required this.wifiConnected,
    required this.mqttConnected,
    required this.cycleActive,
    required this.cyclePaused,
    required this.cycleId,
    required this.litersDelivered,
    required this.startedBy,
    required this.cycleStartUnix,
  });

  // Local (phone) time the current cycle started, or null if none active.
  // Firmware's RTC is IST wall-clock stored as a unix value (not true UTC
  // — see the firmware's RTC comments), so this is parsed the same way:
  // treat the epoch value's calendar/clock fields as already being IST,
  // don't apply an additional timezone shift on top.
  DateTime? get cycleStartTime => (cycleActive && cycleStartUnix > 0)
      ? DateTime.fromMillisecondsSinceEpoch(cycleStartUnix * 1000, isUtc: true)
      : null;

  factory DeviceStatus.fromJson(Map<String, dynamic> j) => DeviceStatus(
        deviceId:        j['device_id']        ?? '',
        firmware:        j['firmware']          ?? '',
        pumpOn:          j['pump_on']           ?? false,
        rtcTime:         j['rtc_time']          ?? '--:--',
        rtcDate:         j['rtc_date']          ?? '--/--/----',
        rtcSet:          j['rtc_set']           ?? false,
        wifiRssi:        (j['wifi_rssi']        ?? 0) as int,
        wifiConnected:   j['wifi_connected']    ?? false,
        mqttConnected:   j['mqtt_connected']    ?? false,
        cycleActive:     j['cycle_active']      ?? false,
        cyclePaused:     j['cycle_paused']      ?? false,
        cycleId:         (j['cycle_id']         ?? 0) as int,
        litersDelivered: (j['liters_delivered'] ?? 0).toDouble(),
        startedBy:       j['started_by']        ?? '',
        cycleStartUnix:  (j['cycle_start_unix'] ?? 0) as int,
      );

  static DeviceStatus empty() => DeviceStatus(
        deviceId: '', firmware: '', pumpOn: false,
        rtcTime: '--:--', rtcDate: '--/--/----', rtcSet: false,
        wifiRssi: 0, wifiConnected: false, mqttConnected: false,
        cycleActive: false, cyclePaused: false, cycleId: 0,
        litersDelivered: 0.0, startedBy: '', cycleStartUnix: 0,
      );
}
