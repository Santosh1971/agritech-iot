import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';

/// Bench station settings -- set once per bench (see
/// PRODUCTION_TOOL_SPEC.md section 7 "one-time-per-session field").
class SettingsScreen extends ConsumerStatefulWidget {
  const SettingsScreen({super.key});
  @override
  ConsumerState<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends ConsumerState<SettingsScreen> {
  late final TextEditingController _flashBridge;
  late final TextEditingController _jigHost;
  late final TextEditingController _testApSsid;
  late final TextEditingController _testApPassword;
  late final TextEditingController _ppl;
  late final TextEditingController _pulseCount;
  late final TextEditingController _operator;
  late final TextEditingController _station;
  late String _env;

  @override
  void initState() {
    super.initState();
    final c = ref.read(benchConfigProvider);
    _flashBridge = TextEditingController(text: c.flashBridgeHost);
    _jigHost = TextEditingController(text: c.jigHost);
    _testApSsid = TextEditingController(text: c.testApSsid);
    _testApPassword = TextEditingController(text: c.testApPassword);
    _ppl = TextEditingController(text: c.expectedCalibrationPpl.toString());
    _pulseCount = TextEditingController(text: c.flowTestPulseCount.toString());
    _operator = TextEditingController(text: c.operatorName);
    _station = TextEditingController(text: c.stationName);
    _env = c.firmwareEnv;
  }

  @override
  void dispose() {
    for (final c in [_flashBridge, _jigHost, _testApSsid, _testApPassword, _ppl, _pulseCount, _operator, _station]) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> _save() async {
    await ref.read(benchConfigProvider.notifier).update((c) => c.copyWith(
          flashBridgeHost: _flashBridge.text.trim(),
          jigHost: _jigHost.text.trim(),
          firmwareEnv: _env,
          testApSsid: _testApSsid.text.trim(),
          testApPassword: _testApPassword.text,
          expectedCalibrationPpl: int.tryParse(_ppl.text) ?? 450,
          flowTestPulseCount: int.tryParse(_pulseCount.text) ?? 450,
          operatorName: _operator.text.trim(),
          stationName: _station.text.trim(),
        ));
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Saved')));
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Bench Settings')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          const Text('Flash Bridge', style: TextStyle(fontWeight: FontWeight.bold)),
          TextField(
            controller: _flashBridge,
            decoration: const InputDecoration(
                labelText: 'host:port', hintText: '192.168.1.50:8787'),
          ),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            value: _env,
            decoration: const InputDecoration(labelText: 'Firmware environment'),
            items: const [
              DropdownMenuItem(value: 'esp32dev', child: Text('esp32dev (DS3231, field build)')),
              DropdownMenuItem(value: 'esp32dev_ds1307', child: Text('esp32dev_ds1307 (bench DS1307)')),
            ],
            onChanged: (v) => setState(() => _env = v ?? _env),
          ),
          const Divider(height: 32),
          const Text('Test Jig', style: TextStyle(fontWeight: FontWeight.bold)),
          TextField(
            controller: _jigHost,
            decoration: const InputDecoration(labelText: 'Jig host', hintText: 'fg1jig.local'),
          ),
          const Divider(height: 32),
          const Text('Bench LAN (Test AP)', style: TextStyle(fontWeight: FontWeight.bold)),
          TextField(controller: _testApSsid, decoration: const InputDecoration(labelText: 'SSID')),
          TextField(
            controller: _testApPassword,
            decoration: const InputDecoration(labelText: 'Password'),
            obscureText: true,
          ),
          const Divider(height: 32),
          const Text('Test parameters', style: TextStyle(fontWeight: FontWeight.bold)),
          TextField(
            controller: _ppl,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: 'Expected pulses/liter'),
          ),
          TextField(
            controller: _pulseCount,
            keyboardType: TextInputType.number,
            decoration: const InputDecoration(labelText: 'Flow test pulse count'),
          ),
          const Divider(height: 32),
          const Text('Session', style: TextStyle(fontWeight: FontWeight.bold)),
          TextField(controller: _operator, decoration: const InputDecoration(labelText: 'Operator name')),
          TextField(controller: _station, decoration: const InputDecoration(labelText: 'Station name')),
          const SizedBox(height: 24),
          ElevatedButton(onPressed: _save, child: const Text('Save')),
        ],
      ),
    );
  }
}
