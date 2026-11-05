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
  // Accumulated ACTIVE (relay-on) seconds for the current cycle — the
  // basis for duration-mode completion on the firmware side. Outage/
  // paused time doesn't count, only genuine pump-on time.
  final int elapsedSeconds;

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
    required this.elapsedSeconds,
  });

  // Local (phone) time the current cycle started, or null if none active.
  // Firmware's RTC is IST wall-clock stored as a unix value (not true UTC
  // — see the firmware's RTC comments), so this is parsed the same way:
  // treat the epoch value's calendar/clock fields as already being IST,
  // don't apply an additional timezone shift on top.
  DateTime? get cycleStartTime => (cycleActive && cycleStartUnix > 0)
      ? DateTime.fromMillisecondsSinceEpoch(cycleStartUnix * 1000, isUtc: true)
      : null;

  // The device's own current wall-clock time, parsed from rtc_date/rtc_time
  // — same "reinterpret as UTC" convention as cycleStartTime above, so it
  // compares directly against HistoryEntry.dateTime with no phone-vs-device
  // clock/timezone mismatch. This is what "today" should mean for anything
  // history-related: the phone's own DateTime.now() can silently disagree
  // with the device (RTC drift, no NTP, or a deliberately-set test time),
  // and using the phone's clock as the boundary can clip out entries the
  // device considers perfectly current. Null until a real status has been
  // received (rtcSet false, or still the placeholder '--:--'/'--/--/----').
  DateTime? get currentTime {
    if (!rtcSet) return null;
    final dateParts = rtcDate.split('/');
    final timeParts = rtcTime.split(':');
    if (dateParts.length != 3 || timeParts.length != 2) return null;
    final day    = int.tryParse(dateParts[0]);
    final month  = int.tryParse(dateParts[1]);
    final year   = int.tryParse(dateParts[2]);
    final hour   = int.tryParse(timeParts[0]);
    final minute = int.tryParse(timeParts[1]);
    if (day == null || month == null || year == null || hour == null || minute == null) return null;
    return DateTime.utc(year, month, day, hour, minute);
  }

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
        elapsedSeconds:  (j['elapsed_seconds']  ?? 0) as int,
      );

  static DeviceStatus empty() => DeviceStatus(
        deviceId: '', firmware: '', pumpOn: false,
        rtcTime: '--:--', rtcDate: '--/--/----', rtcSet: false,
        wifiRssi: 0, wifiConnected: false, mqttConnected: false,
        cycleActive: false, cyclePaused: false, cycleId: 0,
        litersDelivered: 0.0, startedBy: '', cycleStartUnix: 0,
        elapsedSeconds: 0,
      );
}
