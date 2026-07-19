import 'cycle.dart';
class HistoryEntry {
  final int timestamp;
  final int cycleId;
  final String cycleName;
  final OperationMode mode;
  final double litersDelivered;
  final int durationSeconds;
  final String status;
  HistoryEntry({
    required this.timestamp,
    required this.cycleId,
    required this.cycleName,
    required this.mode,
    required this.litersDelivered,
    required this.durationSeconds,
    required this.status,
  });
  // Firmware writes IST wall-clock fields directly to the DS3231 (see
  // RTCManager::syncFromTm), so the resulting "unix" timestamp already
  // encodes IST field values, not true UTC. Parsing with isUtc:true avoids
  // a second +5:30 shift being applied on top.
  DateTime get dateTime =>
      DateTime.fromMillisecondsSinceEpoch(timestamp * 1000, isUtc: true);
  String get durationStr {
    final m = durationSeconds ~/ 60;
    final s = durationSeconds % 60;
    return m > 0 ? '${m}m ${s}s' : '${s}s';
  }
  factory HistoryEntry.fromJson(Map<String, dynamic> j) => HistoryEntry(
        timestamp:       j['ts']     ?? 0,
        cycleId:         j['cid']    ?? 0,
        cycleName:       j['name']   ?? '',
        mode:            OperationMode.values[j['mode'] ?? 0],
        litersDelivered: (j['liters'] ?? 0).toDouble(),
        durationSeconds: j['dur']    ?? 0,
        status:          j['status'] ?? '',
      );
  Map<String, dynamic> toJson() => {
        'ts':      timestamp,
        'cid':     cycleId,
        'name':    cycleName,
        'mode':    mode.index,
        'liters':  litersDelivered,
        'dur':     durationSeconds,
        'status':  status,
      };
}
