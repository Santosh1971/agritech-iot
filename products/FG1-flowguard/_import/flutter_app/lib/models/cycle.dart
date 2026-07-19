import '../utils/time_format.dart';

enum OperationMode { literBased, timeBased, timeWindowLiter }

class Cycle {
  final int id;
  final String name;
  final int startHour;
  final int startMinute;
  final int endHour;
  final int endMinute;
  final OperationMode mode;
  final double targetLiters;
  final bool enabled;

  Cycle({
    required this.id,
    required this.name,
    required this.startHour,
    required this.startMinute,
    required this.endHour,
    required this.endMinute,
    required this.mode,
    required this.targetLiters,
    required this.enabled,
  });

  String get startTimeStr => formatTime12(startHour, startMinute);
  String get endTimeStr => formatTime12(endHour, endMinute);
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
        'eh':      endHour,
        'em':      endMinute,
        'mode':    mode.index,
        'liters':  targetLiters,
        'enabled': enabled,
      };

  factory Cycle.fromJson(Map<String, dynamic> j) => Cycle(
        id:           j['id']      ?? 0,
        name:         j['name']    ?? '',
        startHour:    j['sh']      ?? 0,
        startMinute:  j['sm']      ?? 0,
        endHour:      j['eh']      ?? 0,
        endMinute:    j['em']      ?? 0,
        mode:         OperationMode.values[j['mode'] ?? 0],
        targetLiters: (j['liters'] ?? 0).toDouble(),
        enabled:      j['enabled'] ?? true,
      );

  Cycle copyWith({
    int? id, String? name,
    int? startHour, int? startMinute,
    int? endHour, int? endMinute,
    OperationMode? mode, double? targetLiters, bool? enabled,
  }) => Cycle(
        id:           id           ?? this.id,
        name:         name         ?? this.name,
        startHour:    startHour    ?? this.startHour,
        startMinute:  startMinute  ?? this.startMinute,
        endHour:      endHour      ?? this.endHour,
        endMinute:    endMinute    ?? this.endMinute,
        mode:         mode         ?? this.mode,
        targetLiters: targetLiters ?? this.targetLiters,
        enabled:      enabled      ?? this.enabled,
      );
}
