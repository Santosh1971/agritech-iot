import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../models/device_status.dart';

class ManualControlScreen extends ConsumerWidget {
  const ManualControlScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connected = ref.watch(deviceConnectedProvider);
    final statusAsync = ref.watch(deviceStatusProvider);
    final status = statusAsync.valueOrNull ?? DeviceStatus.empty();
    final auto = status.state == SchedulerState.running || status.state == SchedulerState.paused;
    final anyValveOpen = status.valves.any((v) => v);

    return Scaffold(
      appBar: AppBar(title: const Text('Manual Control', style: TextStyle(fontWeight: FontWeight.w600)), centerTitle: true),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          if (auto)
            Container(
              padding: const EdgeInsets.all(12),
              margin: const EdgeInsets.only(bottom: 16),
              decoration: BoxDecoration(color: Colors.orange.withOpacity(0.1), borderRadius: BorderRadius.circular(8)),
              child: Row(children: [
                const Icon(Icons.info_outline, color: Colors.orange, size: 18),
                const SizedBox(width: 8),
                const Expanded(
                    child: Text('A program is currently running. Manual changes override it now; the schedule picks back up once you release/finish.',
                        style: TextStyle(fontSize: 12, color: Colors.deepOrange))),
              ]),
            ),
          _RelayTile(
            label: status.relayNames.pump,
            subtitle: 'Auto-follows valves — turns on whenever any valve below is on',
            icon: Icons.water_drop,
            on: status.pump,
            enabled: false,
            onChanged: null,
          ),
          for (int i = 0; i < 4; i++)
            _RelayTile(
              label: status.relayNames.valveName(i),
              subtitle: 'RL${i + 3}',
              icon: Icons.opacity,
              on: status.valves.length > i ? status.valves[i] : false,
              enabled: connected,
              onChanged: (v) => ref.read(deviceServiceProvider).manualSet('valve${i + 1}', v),
            ),
          _RelayTile(
            label: status.relayNames.dosing,
            subtitle: anyValveOpen ? 'RL2 — fertigation pump' : 'RL2 — needs a valve open first (won\'t dose into a closed line)',
            icon: Icons.science,
            on: status.dosing,
            enabled: connected && anyValveOpen,
            onChanged: (v) => ref.read(deviceServiceProvider).manualSet('dosing', v),
          ),
          const SizedBox(height: 16),
          SizedBox(
            width: double.infinity,
            child: FilledButton.icon(
              onPressed: connected ? () => ref.read(deviceServiceProvider).forceStop() : null,
              style: FilledButton.styleFrom(backgroundColor: Colors.red, padding: const EdgeInsets.symmetric(vertical: 14)),
              icon: const Icon(Icons.stop_circle),
              label: const Text('Force Stop Everything'),
            ),
          ),
        ],
      ),
    );
  }
}

class _RelayTile extends StatelessWidget {
  final String label, subtitle;
  final IconData icon;
  final bool on;
  final bool enabled;
  final ValueChanged<bool>? onChanged;
  const _RelayTile({required this.label, required this.subtitle, required this.icon, required this.on, required this.enabled, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    final color = on ? const Color(0xFF2196F3) : Colors.grey;
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: SwitchListTile(
        value: on,
        onChanged: enabled ? onChanged : null,
        secondary: Icon(icon, color: color),
        title: Text(label, style: const TextStyle(fontWeight: FontWeight.w600)),
        subtitle: Text(subtitle, style: const TextStyle(fontSize: 12, color: Colors.grey)),
      ),
    );
  }
}
