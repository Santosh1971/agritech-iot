import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/device_status.dart';
import '../providers/providers.dart';

/// Give each relay / sensor a farm name ("Tomato A" instead of
/// "Valve 1"). The fixed hardware tag (_R3 etc.) is still shown after it.
class RenameOutputsScreen extends ConsumerStatefulWidget {
  const RenameOutputsScreen({super.key});
  @override
  ConsumerState<RenameOutputsScreen> createState() => _RenameOutputsScreenState();
}

class _RenameOutputsScreenState extends ConsumerState<RenameOutputsScreen> {
  late final Map<String, TextEditingController> _c;

  @override
  void initState() {
    super.initState();
    final n = ref.read(deviceStatusProvider).valueOrNull?.relayNames ?? const RelayNames();
    _c = {
      'pump': TextEditingController(text: n.pump),
      'dosing': TextEditingController(text: n.dosing),
      for (var i = 0; i < 4; i++) 'valve$i': TextEditingController(text: n.valveName(i)),
      'pressure1': TextEditingController(text: n.pressure1),
      'pressure2': TextEditingController(text: n.pressure2),
      'flow': TextEditingController(text: n.flow),
    };
  }

  @override
  void dispose() {
    for (final c in _c.values) {
      c.dispose();
    }
    super.dispose();
  }

  String _v(String k, String fallback) => _c[k]!.text.trim().isEmpty ? fallback : _c[k]!.text.trim();

  void _save() {
    final old = ref.read(deviceStatusProvider).valueOrNull?.relayNames ?? const RelayNames();
    ref.read(localServiceProvider).setRelayNames(
          pump: _v('pump', 'Pump'),
          dosing: _v('dosing', 'Dosing'),
          valves: [for (var i = 0; i < 4; i++) _v('valve$i', 'Valve ${i + 1}')],
          pressure1: _v('pressure1', 'Pressure 1'),
          pressure2: _v('pressure2', 'Pressure 2'),
          flow: _v('flow', 'Water Meter'),
          waterUpper: old.waterUpper,
          waterLower: old.waterLower,
        );
    ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Names saved')));
    Navigator.pop(context);
  }

  Widget _field(String key, String label, String tag) => Padding(
        padding: const EdgeInsets.only(bottom: 12),
        child: TextField(
          controller: _c[key],
          maxLength: 15,
          decoration: InputDecoration(
            labelText: label,
            suffixText: tag,
            border: const OutlineInputBorder(),
            counterText: '',
          ),
        ),
      );

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Rename outputs'), centerTitle: true),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        Text('Name each output after your field or crop. The hardware tag stays so '
            'the electrician still knows which terminal it is.',
            style: TextStyle(color: Colors.grey.shade600)),
        const SizedBox(height: 16),
        _field('pump', 'Pump', '_R1'),
        _field('dosing', 'Dosing / fertilizer', '_R2'),
        for (var i = 0; i < 4; i++) _field('valve$i', 'Valve ${i + 1}', '_R${i + 3}'),
        _field('pressure1', 'Pressure sensor 1', '_P1'),
        _field('pressure2', 'Pressure sensor 2', '_P2'),
        _field('flow', 'Water meter', '_FL'),
        const SizedBox(height: 8),
        FilledButton(onPressed: _save, child: const Text('Save names')),
      ]),
    );
  }
}
