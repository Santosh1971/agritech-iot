import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../../common/demo.dart';
import '../providers/providers.dart';
import '../utils/time_format.dart';

/// Demo version of FG1 Settings: device info is simulated, and the
/// WiFi / cloud / pairing sections are replaced by demo controls.
class SettingsScreen extends ConsumerWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final s = ref.watch(deviceStatusProvider).valueOrNull;
    return Scaffold(
      appBar: AppBar(title: const Text('Settings'), centerTitle: true),
      body: ListView(padding: const EdgeInsets.symmetric(vertical: 8), children: [
        Card(
          child: ListTile(
            leading: const Icon(Icons.check_circle, color: Color(0xFF4CAF50), size: 32),
            title: const Text('Demo device online',
                style: TextStyle(fontWeight: FontWeight.w600)),
            subtitle: s == null
                ? null
                : Text('${s.deviceId} • Firmware ${s.firmware}\n'
                    'WiFi ${s.wifiRssi} dBm • Clock ${s.rtcDate} ${formatTime12FromString(s.rtcTime)}'),
            isThreeLine: true,
          ),
        ),
        DemoSettingsCard(
          productName: 'FG1',
          onReset: () => ref.read(fg1DemoServiceProvider).reset(),
        ),
        const Card(
          child: Padding(
            padding: EdgeInsets.all(16),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('On the real FG1 FlowGuard',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
              SizedBox(height: 8),
              Text('• Works on the controller\'s own WiFi (no internet needed) or from anywhere over the internet\n'
                  '• Schedules keep running even if the phone is off\n'
                  '• Remembers a cycle through a power cut and finishes it when power returns\n'
                  '• Flow meter counts every litre delivered\n'
                  '• Battery-backed clock keeps time without WiFi'),
            ]),
          ),
        ),
      ]),
    );
  }
}
