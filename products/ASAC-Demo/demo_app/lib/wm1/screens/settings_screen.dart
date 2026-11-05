import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../common/demo.dart';
import '../providers/providers.dart';
import 'hardware_config_screen.dart';
import 'history_screen.dart';
import 'rename_outputs_screen.dart';

/// Demo version of WM1-Mini Settings: WiFi / cloud / pairing sections are
/// replaced by demo controls and "what if" buttons (power cut, dry tank).
class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = ref.watch(deviceStatusProvider).valueOrNull;
    final svc = ref.watch(wm1DemoServiceProvider);

    Widget tile(IconData icon, Color color, String title, String sub, VoidCallback onTap) =>
        ListTile(
          leading: Icon(icon, color: color),
          title: Text(title, style: const TextStyle(fontWeight: FontWeight.w500)),
          subtitle: Text(sub, style: const TextStyle(fontSize: 12)),
          trailing: const Icon(Icons.chevron_right),
          onTap: onTap,
        );

    return Scaffold(
      appBar: AppBar(title: const Text('Settings'), centerTitle: true),
      body: ListView(padding: const EdgeInsets.symmetric(vertical: 8), children: [
        Card(
          child: ListTile(
            leading: const Icon(Icons.check_circle, color: Color(0xFF4CAF50), size: 32),
            title: const Text('Demo device online', style: TextStyle(fontWeight: FontWeight.w600)),
            subtitle: s == null
                ? null
                : Text('${s.deviceId} • Clock ${s.rtcDate} ${s.rtcTime}\n'
                    'WiFi ${s.wifiRssi} dBm • Battery ${s.batteryVolts.toStringAsFixed(1)} V'),
            isThreeLine: true,
          ),
        ),
        Card(
          child: Column(children: [
            tile(Icons.edit, Colors.teal, 'Rename outputs',
                'Call valves by your crop or field name',
                () => Navigator.push(context,
                    MaterialPageRoute(builder: (_) => const RenameOutputsScreen()))),
            tile(Icons.tune, Colors.indigo, 'Hardware configuration',
                'Choose which sensors and dosing are fitted',
                () => Navigator.push(context,
                    MaterialPageRoute(builder: (_) => const HardwareConfigScreen()))),
            tile(Icons.bar_chart, Colors.blue, 'Run history',
                'Time and litres by day, scheduled and manual',
                () => Navigator.push(context,
                    MaterialPageRoute(builder: (_) => const HistoryScreen()))),
          ]),
        ),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Text('Try what happens if…',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
              const SizedBox(height: 4),
              Text('Start a program first, then try these. The controller pauses safely '
                  'and carries on from where it stopped.',
                  style: TextStyle(fontSize: 12, color: Colors.grey.shade600)),
              const SizedBox(height: 8),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                secondary: Icon(Icons.power_off, color: svc.powerOk ? Colors.grey : Colors.red),
                title: const Text('Power cut'),
                subtitle: const Text('Mains power to the pump is lost'),
                value: !svc.powerOk,
                onChanged: (v) => v ? svc.simulatePowerLoss() : svc.simulatePowerRestore(),
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                secondary: Icon(Icons.water_drop_outlined,
                    color: svc.waterOk ? Colors.grey : Colors.orange),
                title: const Text('Tank / well runs dry'),
                subtitle: Text(s?.waterLevelEnabled == true
                    ? 'Level switches read dry — pump protected'
                    : 'Turn on "Water level switch" in Hardware configuration first'),
                value: !svc.waterOk,
                onChanged: (v) =>
                    svc.sendRaw({'cmd': v ? 'simulate_water_low' : 'simulate_water_ok'}),
              ),
            ]),
          ),
        ),
        DemoSettingsCard(
          productName: 'WM1-Mini',
          onReset: () => ref.read(wm1DemoServiceProvider).reset(),
        ),
      ]),
    );
  }
}
