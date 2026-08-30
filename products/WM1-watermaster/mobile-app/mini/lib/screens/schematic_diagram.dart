import 'package:flutter/material.dart';
import '../models/device_status.dart';
import '../models/hardware_config.dart';

/// Pictorial "at a glance" view of the Mini's real, fixed hardware —
/// styled after the reference layout Kamta sent (icons + color-coded
/// state + live values along a schematic pipe run), scoped to exactly
/// what THIS board has: one main pump, one doser, up to 2 pressure
/// sensors, one water meter, 4 valves, and an optional water-level
/// switch. Which of the optional items actually show is driven by
/// HardwareConfig (Settings > Hardware Configuration) — nothing here
/// invents a reading for a sensor that was never wired up.
class SchematicDiagram extends StatelessWidget {
  final DeviceStatus status;
  final HardwareConfig config;
  const SchematicDiagram({super.key, required this.status, required this.config});

  static const _onColor = Color(0xFF2E7D32);
  static const _offColor = Color(0xFF9E9E9E);
  static const _warnColor = Color(0xFFE53935);
  static const _pipeColor = Color(0xFFB0BEC5);

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Text('IRRIGATION LINE', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
        const SizedBox(height: 12),
        if (config.hasWaterLevel) ...[
          _waterLevelRow(),
          const SizedBox(height: 8),
          _pipeDown(),
        ],
        _pumpRow(),
        _pipeDown(),
        if (config.hasPressure1 || config.hasPressure2 || config.hasWaterMeter) ...[
          _sensorRow(),
          _pipeDown(),
        ],
        _valveManifold(),
        const SizedBox(height: 16),
        _legend(),
      ]),
    );
  }

  Widget _pipeDown() => Center(
        child: Container(width: 3, height: 16, color: _pipeColor),
      );

  Widget _waterLevelRow() {
    final ok = status.waterLevelOk;
    return Row(children: [
      Icon(Icons.water_drop, color: ok ? Colors.blue : _warnColor, size: 22),
      const SizedBox(width: 8),
      Text('Water Source', style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
      const Spacer(),
      _StatePill(label: ok ? 'Level OK' : 'LOW LEVEL', color: ok ? Colors.blue : _warnColor),
    ]);
  }

  Widget _pumpRow() {
    return Row(children: [
      Expanded(child: _ComponentIcon(icon: Icons.settings, label: 'Main Pump', on: status.pump, onColor: _onColor, offColor: _offColor)),
      if (config.hasDoser) ...[
        const SizedBox(width: 12),
        Expanded(child: _ComponentIcon(icon: Icons.science, label: 'Doser', on: status.dosing, onColor: const Color(0xFF7B1FA2), offColor: _offColor)),
      ],
    ]);
  }

  Widget _sensorRow() {
    return Row(children: [
      if (config.hasPressure1)
        Expanded(child: _ReadingTile(icon: Icons.speed, label: 'Pressure 1', value: '${status.pressure1Bar.toStringAsFixed(2)} bar')),
      if (config.hasPressure1 && (config.hasPressure2 || config.hasWaterMeter)) const SizedBox(width: 8),
      if (config.hasPressure2)
        Expanded(child: _ReadingTile(icon: Icons.speed, label: 'Pressure 2', value: '${status.pressure2Bar.toStringAsFixed(2)} bar')),
      if (config.hasPressure2 && config.hasWaterMeter) const SizedBox(width: 8),
      if (config.hasWaterMeter)
        Expanded(child: _ReadingTile(icon: Icons.water_damage_outlined, label: 'Water Meter', value: '${status.flowRateLpm.toStringAsFixed(1)} L/m')),
    ]);
  }

  Widget _valveManifold() {
    return Column(children: [
      Container(height: 3, color: _pipeColor),
      const SizedBox(height: 10),
      Row(children: [
        for (int i = 0; i < 4; i++) ...[
          if (i > 0) const SizedBox(width: 8),
          Expanded(
            child: Column(children: [
              Container(width: 3, height: 12, color: _pipeColor),
              _ComponentIcon(
                icon: Icons.opacity,
                label: status.relayNames.valveName(i),
                on: status.valves.length > i ? status.valves[i] : false,
                onColor: _onColor,
                offColor: _offColor,
                compact: true,
              ),
            ]),
          ),
        ],
      ]),
    ]);
  }

  Widget _legend() {
    return Wrap(spacing: 12, runSpacing: 6, children: [
      _legendItem(_onColor, 'Running / Open'),
      _legendItem(_offColor, 'Off / Closed'),
      if (config.hasWaterLevel) _legendItem(_warnColor, 'Low Level'),
    ]);
  }

  Widget _legendItem(Color color, String label) => Row(mainAxisSize: MainAxisSize.min, children: [
        Container(width: 10, height: 10, decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
        const SizedBox(width: 4),
        Text(label, style: const TextStyle(fontSize: 11, color: Colors.grey)),
      ]);
}

class _ComponentIcon extends StatelessWidget {
  final IconData icon;
  final String label;
  final bool on;
  final Color onColor;
  final Color offColor;
  final bool compact;
  const _ComponentIcon({required this.icon, required this.label, required this.on, required this.onColor, required this.offColor, this.compact = false});

  @override
  Widget build(BuildContext context) {
    final color = on ? onColor : offColor;
    return Column(children: [
      Container(
        width: compact ? 44 : 56,
        height: compact ? 44 : 56,
        decoration: BoxDecoration(
          color: color.withOpacity(0.12),
          shape: BoxShape.circle,
          border: Border.all(color: color, width: 2),
        ),
        child: Icon(icon, color: color, size: compact ? 20 : 26),
      ),
      const SizedBox(height: 4),
      Text(label, textAlign: TextAlign.center, maxLines: 2, overflow: TextOverflow.ellipsis,
          style: TextStyle(fontSize: compact ? 10 : 12, fontWeight: FontWeight.w600, color: color)),
    ]);
  }
}

class _ReadingTile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  const _ReadingTile({required this.icon, required this.label, required this.value});

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 8),
        decoration: BoxDecoration(color: Colors.grey.shade100, borderRadius: BorderRadius.circular(8)),
        child: Column(children: [
          Icon(icon, size: 16, color: Colors.grey.shade700),
          const SizedBox(height: 2),
          Text(value, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 13)),
          Text(label, style: TextStyle(fontSize: 9, color: Colors.grey.shade600)),
        ]),
      );
}

class _StatePill extends StatelessWidget {
  final String label;
  final Color color;
  const _StatePill({required this.label, required this.color});
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
        decoration: BoxDecoration(color: color.withOpacity(0.12), borderRadius: BorderRadius.circular(10)),
        child: Text(label, style: TextStyle(color: color, fontSize: 11, fontWeight: FontWeight.w600)),
      );
}
