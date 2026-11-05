import '../utils/time_format.dart';

enum OperationMode { literBased, timeBased, timeWindowLiter }

class Cycle {
  final int id;
  final String name;
  final int startHour;
  final int startMinute;
  // Replaces the old fixed endHour/endMinute clock time. A fixed end
  // time meant a power outage mid-cycle silently shortened the actual
  // run (pump stops at the same wall-clock time regardless of how long
  // it was off). Duration is tracked by the firmware as accumulated
  // ACTIVE (relay-on) seconds instead, so an outage doesn't count
  // against it — the same way outage time never counted against a liter
  // target.
  final int durationMinutes;
  final OperationMode mode;
  final double targetLiters;
  final bool enabled;

  Cycle({
    required this.id,
    required this.name,
    required this.startHour,
    required this.startMinute,
    required this.durationMinutes,
    required this.mode,
    required this.targetLiters,
    required this.enabled,
  });

  String get startTimeStr => formatTime12(startHour, startMinute);

  String get durationStr {
    final h = durationMinutes ~/ 60;
    final m = durationMinutes % 60;
    if (h > 0 && m > 0) return '${h}h ${m}m';
    if (h > 0) return '${h}h';
    return '${m}m';
  }

  String get modeLabel => switch (mode) {
        OperationMode.literBased       => 'Liter Based',
        OperationMode.timeBased        => 'Time Based',
        OperationMode.timeWindowLiter  => 'Time Window + Liter',
      };

  Map<String, dynamic> toJson() => {
        'id':      id,
        'name':    name,
        'sh':      startHour,
        'sm':      startMinute,
        'dur':     durationMinutes,
        'mode':    mode.index,
        'liters':  targetLiters,
        'enabled': enabled,
      };

  factory Cycle.fromJson(Map<String, dynamic> j) => Cycle(
        id:              j['id']   ?? 0,
        name:            j['name'] ?? '',
        startHour:       j['sh']   ?? 0,
        startMinute:     j['sm']   ?? 0,
        durationMinutes: j['dur']  ?? 0,
        mode:            OperationMode.values[j['mode'] ?? 0],
        targetLiters:    (j['liters'] ?? 0).toDouble(),
        enabled:         j['enabled'] ?? true,
      );

  Cycle copyWith({
    int? id, String? name,
    int? startHour, int? startMinute,
    int? durationMinutes,
    OperationMode? mode, double? targetLiters, bool? enabled,
  }) => Cycle(
        id:              id              ?? this.id,
        name:            name            ?? this.name,
        startHour:       startHour       ?? this.startHour,
        startMinute:     startMinute     ?? this.startMinute,
        durationMinutes: durationMinutes ?? this.durationMinutes,
        mode:            mode            ?? this.mode,
        targetLiters:    targetLiters    ?? this.targetLiters,
        enabled:         enabled         ?? this.enabled,
      );
}
