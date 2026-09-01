import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';

/// Which optional I/O this specific installation actually has wired up
/// — the schematic Dashboard only draws/reads what's marked present
/// here. Main Pump and the 4 valves aren't listed: every Mini has them
/// by definition, there's nothing to configure.
class HardwareConfigScreen extends ConsumerWidget {
  const HardwareConfigScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final config = ref.watch(hardwareConfigProvider);
    final notifier = ref.read(hardwareConfigProvider.notifier);

    return Scaffold(
      appBar: AppBar(title: const Text('Hardware Configuration', style: TextStyle(fontWeight: FontWeight.w600)), centerTitle: true),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text(
            'Main Pump and the 4 valves are always shown — every Mini has '
            'them. Toggle the rest to match what\'s actually wired on this '
            'installation, so the Dashboard doesn\'t show a reading for a '
            'sensor that was never connected.',
            style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
          ),
          const SizedBox(height: 16),
          _Section(children: [
            _Toggle(
              icon: Icons.science,
              label: 'Doser / Fertigation Pump',
              subtitle: 'RL2',
              value: config.hasDoser,
              onChanged: (v) => notifier.update(config.copyWith(hasDoser: v)),
            ),
            _Toggle(
              icon: Icons.speed,
              label: 'Pressure Sensor 1',
              value: config.hasPressure1,
              onChanged: (v) => notifier.update(config.copyWith(hasPressure1: v)),
            ),
            _Toggle(
              icon: Icons.speed,
              label: 'Pressure Sensor 2',
              value: config.hasPressure2,
              onChanged: (v) => notifier.update(config.copyWith(hasPressure2: v)),
            ),
            _Toggle(
              icon: Icons.water_damage_outlined,
              label: 'Water Meter (Flow)',
              value: config.hasWaterMeter,
              onChanged: (v) => notifier.update(config.copyWith(hasWaterMeter: v)),
            ),
            _Toggle(
              icon: Icons.waves,
              label: 'Water Level Sensor (L1 + L2)',
              subtitle: 'IN2 / IN3',
              value: config.hasWaterLevel,
              onChanged: (v) {
                notifier.update(config.copyWith(hasWaterLevel: v));
                // This one isn't purely cosmetic: it also arms/disarms the
                // firmware's actual dry-run pause behavior, so the device
                // needs to be told directly, not just the app's own display
                // preference — see set_water_level_enabled's doc comment in
                // CommandHandler.h. A borewell farmer who leaves this off
                // never has L1/L2 read at all.
                ref.read(deviceServiceProvider).sendRaw({'cmd': 'set_water_level_enabled', 'enabled': v});
              },
            ),
          ]),
          if (config.hasWaterLevel) ...[
            const SizedBox(height: 16),
            Text('DRY-RUN PROTECTION', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey.shade600)),
            const SizedBox(height: 8),
            Text(
              'If both L1 and L2 read dry (water has receded below L1, the '
              'lower switch), irrigation pauses automatically — same '
              'freeze/resume behavior as a power outage — and resumes once '
              'water is back. No real float switches are wired up yet, so '
              'use these to bench-test the pause without them.',
              style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
            ),
            const SizedBox(height: 12),
            _Section(children: [
              ListTile(
                leading: const Icon(Icons.water_drop_outlined, color: Colors.orange),
                title: const Text('Simulate Water Low', style: TextStyle(fontWeight: FontWeight.w500)),
                subtitle: const Text('Both L1 and L2 dry', style: TextStyle(fontSize: 12, color: Colors.grey)),
                onTap: () => ref.read(deviceServiceProvider).sendRaw({'cmd': 'simulate_water_low'}),
              ),
              ListTile(
                leading: const Icon(Icons.water_drop, color: Colors.blue),
                title: const Text('Simulate Water OK', style: TextStyle(fontWeight: FontWeight.w500)),
                subtitle: const Text('L1 and L2 restored', style: TextStyle(fontSize: 12, color: Colors.grey)),
                onTap: () => ref.read(deviceServiceProvider).sendRaw({'cmd': 'simulate_water_ok'}),
              ),
            ]),
          ],
        ],
      ),
    );
  }
}

class _Section extends StatelessWidget {
  final List<Widget> children;
  const _Section({required this.children});
  @override
  Widget build(BuildContext context) => Container(
        decoration: BoxDecoration(
          color: Theme.of(context).cardColor,
          borderRadius: BorderRadius.circular(12),
          boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
        ),
        child: Column(
          children: children.asMap().entries.map((e) => Column(children: [
                e.value,
                if (e.key < children.length - 1) const Divider(height: 1, indent: 56),
              ])).toList(),
        ),
      );
}

class _Toggle extends StatelessWidget {
  final IconData icon;
  final String label;
  final String? subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;
  const _Toggle({required this.icon, required this.label, this.subtitle, required this.value, required this.onChanged});

  @override
  Widget build(BuildContext context) => SwitchListTile(
        value: value,
        onChanged: onChanged,
        secondary: Icon(icon, color: value ? const Color(0xFF2196F3) : Colors.grey),
        title: Text(label, style: const TextStyle(fontWeight: FontWeight.w500)),
        subtitle: subtitle != null ? Text(subtitle!, style: const TextStyle(fontSize: 12, color: Colors.grey)) : null,
      );
}
