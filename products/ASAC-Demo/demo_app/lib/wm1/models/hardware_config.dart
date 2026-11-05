/// Which of the Mini's optional I/O channels are actually wired up on
/// THIS installation — purely a display preference, stored on the
/// phone, not pushed to the device. The main pump and the 4 valves are
/// always shown (every Mini has them by definition); everything here
/// is optional because not every farmer wires up dosing, both pressure
/// sensors, a flow meter, or a level switch. Showing a reading for a
/// sensor that was never connected would just be a meaningless number,
/// so the schematic dashboard hides whatever isn't marked present here.
class HardwareConfig {
  final bool hasDoser;
  final bool hasPressure1;
  final bool hasPressure2;
  final bool hasWaterMeter;
  final bool hasWaterLevel;

  const HardwareConfig({
    this.hasDoser = true,
    this.hasPressure1 = true,
    this.hasPressure2 = false,
    this.hasWaterMeter = true,
    this.hasWaterLevel = false,
  });

  HardwareConfig copyWith({
    bool? hasDoser,
    bool? hasPressure1,
    bool? hasPressure2,
    bool? hasWaterMeter,
    bool? hasWaterLevel,
  }) =>
      HardwareConfig(
        hasDoser: hasDoser ?? this.hasDoser,
        hasPressure1: hasPressure1 ?? this.hasPressure1,
        hasPressure2: hasPressure2 ?? this.hasPressure2,
        hasWaterMeter: hasWaterMeter ?? this.hasWaterMeter,
        hasWaterLevel: hasWaterLevel ?? this.hasWaterLevel,
      );

  factory HardwareConfig.fromJson(Map<String, dynamic> j) => HardwareConfig(
        hasDoser: j['hasDoser'] as bool? ?? true,
        hasPressure1: j['hasPressure1'] as bool? ?? true,
        hasPressure2: j['hasPressure2'] as bool? ?? false,
        hasWaterMeter: j['hasWaterMeter'] as bool? ?? true,
        hasWaterLevel: j['hasWaterLevel'] as bool? ?? false,
      );

  Map<String, dynamic> toJson() => {
        'hasDoser': hasDoser,
        'hasPressure1': hasPressure1,
        'hasPressure2': hasPressure2,
        'hasWaterMeter': hasWaterMeter,
        'hasWaterLevel': hasWaterLevel,
      };
}
