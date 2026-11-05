import 'device_status.dart';

/// One completed run — either a scheduled program/sequence ("auto") or
/// a single manually-toggled channel ("manual") — see firmware's
/// RunHistory.h for the on-disk record this mirrors.
class HistoryRecord {
  final DateTime start;
  final int durationSec;
  final String name; // "Program - Sequence" for auto; a channel key ("valve3"/"dosing") for manual
  final String source; // "auto" | "manual"
  final double volumeLiters;

  const HistoryRecord({
    required this.start,
    required this.durationSec,
    required this.name,
    required this.source,
    required this.volumeLiters,
  });

  bool get isManual => source == 'manual';

  // Manual records store a stable channel key, not a display name, so
  // a later rename doesn't orphan old history — resolve it against the
  // CURRENT relay names here, same convention used on the schematic.
  String displayName(RelayNames names) {
    if (!isManual) return name;
    return switch (name) {
      'dosing' => names.dosingDisplay,
      'valve1' => names.valveDisplay(0),
      'valve2' => names.valveDisplay(1),
      'valve3' => names.valveDisplay(2),
      'valve4' => names.valveDisplay(3),
      _ => name,
    };
  }

  factory HistoryRecord.fromJson(Map<String, dynamic> j) {
    // Same wall-clock-as-UTC-reinterpretation convention as RTC sync
    // elsewhere in this app (see local_service.dart's syncRtcFromPhone)
    // — the device has no timezone concept, it just stores local time
    // as if it were UTC, so reversing that here gives back local time.
    final epoch = (j['ts'] as num?)?.toInt() ?? 0;
    final utc = DateTime.fromMillisecondsSinceEpoch(epoch * 1000, isUtc: true);
    final local = DateTime(utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second);
    return HistoryRecord(
      start: local,
      durationSec: (j['dur'] as num?)?.toInt() ?? 0,
      name: j['name'] as String? ?? '',
      source: j['src'] as String? ?? 'auto',
      volumeLiters: (j['vol'] as num?)?.toDouble() ?? 0,
    );
  }
}

/// One calendar day's rolled-up totals, for the chart and the daily list.
class DailyHistory {
  final DateTime day;
  final List<HistoryRecord> records;

  const DailyHistory({required this.day, required this.records});

  int get totalDurationSec => records.fold(0, (sum, r) => sum + r.durationSec);
  double get totalVolumeLiters => records.fold(0.0, (sum, r) => sum + r.volumeLiters);

  static List<DailyHistory> groupByDay(List<HistoryRecord> records) {
    final byDay = <DateTime, List<HistoryRecord>>{};
    for (final r in records) {
      final key = DateTime(r.start.year, r.start.month, r.start.day);
      byDay.putIfAbsent(key, () => []).add(r);
    }
    final days = byDay.entries.map((e) => DailyHistory(day: e.key, records: e.value)).toList();
    days.sort((a, b) => a.day.compareTo(b.day));
    return days;
  }
}
