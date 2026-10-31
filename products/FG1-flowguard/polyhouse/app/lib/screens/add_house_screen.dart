import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../services/local_link.dart';
import '../widgets/common.dart';

/// Add a polyhouse. Two ways:
///  1. Phone joined to the controller's own WiFi (FG1P_xxxx, password on the
///     label) -> "Find controller" reads its ID from 192.168.4.1.
///  2. Type the controller ID (for internet access) and optionally its IP
///     on the farm WiFi.
class AddHouseScreen extends StatefulWidget {
  const AddHouseScreen({super.key, required this.store});
  final HouseStore store;
  @override
  State<AddHouseScreen> createState() => _AddHouseScreenState();
}

class _AddHouseScreenState extends State<AddHouseScreen> {
  final _name = TextEditingController(text: 'Polyhouse 1');
  final _id = TextEditingController();
  final _host = TextEditingController(text: '192.168.4.1');
  bool _busy = false;
  String? _found;

  @override
  void initState() {
    super.initState();
    _name.text = 'Polyhouse ${widget.store.houses.where((h) => !h.demo).length + 1}';
  }

  Future<void> _find() async {
    setState(() {
      _busy = true;
      _found = null;
    });
    final l = LocalLink(_host.text.trim());
    final ok = await l.connect();
    String? id;
    if (ok) {
      final r = await l.send({'cmd': 'get_status'});
      final d = r['data'];
      if (d is Map && d['device_id'] is String) id = d['device_id'];
    }
    l.dispose();
    if (!mounted) return;
    setState(() {
      _busy = false;
      _found = id;
      if (id != null) _id.text = id;
    });
    if (id == null) toast(context, 'No controller at ${_host.text}. Is the phone on the FG1P_… WiFi?');
  }

  Future<void> _save({bool demo = false}) async {
    final h = demo
        ? House(name: 'Demo polyhouse', deviceId: 'FG1P_DEMO', host: '', demo: true)
        : House(name: _name.text.trim(), deviceId: _id.text.trim().toUpperCase(), host: _host.text.trim());
    if (!demo && h.deviceId.isEmpty && h.host.isEmpty) {
      toast(context, 'Enter the controller ID or its IP address');
      return;
    }
    await widget.store.add(h);
    if (mounted) Navigator.pop(context);
  }

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Scaffold(
      appBar: AppBar(title: const Text('Add polyhouse')),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        TextField(controller: _name, decoration: const InputDecoration(labelText: 'Name', border: OutlineInputBorder())),
        const SizedBox(height: 20),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Text('At the pump panel', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 16)),
              const SizedBox(height: 6),
              Text('Connect the phone to the controller\'s WiFi "FG1P_xxxx" (password on its label), '
                  'then tap Find.', style: TextStyle(color: cs.onSurfaceVariant)),
              const SizedBox(height: 12),
              TextField(
                  controller: _host,
                  decoration: const InputDecoration(
                      labelText: 'Controller address', helperText: '192.168.4.1 on its own WiFi, or its farm-WiFi IP',
                      border: OutlineInputBorder())),
              const SizedBox(height: 12),
              FilledButton.tonalIcon(
                onPressed: _busy ? null : _find,
                icon: _busy
                    ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.search),
                label: const Text('Find controller'),
              ),
              if (_found != null) ...[
                const SizedBox(height: 8),
                Row(children: [
                  const Icon(Icons.check_circle, color: Colors.green),
                  const SizedBox(width: 6),
                  Text('Found $_found'),
                ]),
              ],
            ]),
          ),
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Text('From anywhere (internet)', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 16)),
              const SizedBox(height: 6),
              Text('Type the controller ID printed on its label, e.g. FG1P_9E74. '
                  'Needs the controller to be on the farm WiFi.', style: TextStyle(color: cs.onSurfaceVariant)),
              const SizedBox(height: 12),
              TextField(
                  controller: _id,
                  textCapitalization: TextCapitalization.characters,
                  decoration: const InputDecoration(labelText: 'Controller ID', border: OutlineInputBorder())),
            ]),
          ),
        ),
        const SizedBox(height: 20),
        FilledButton(onPressed: () => _save(), child: const Text('Save')),
        const SizedBox(height: 8),
        TextButton(onPressed: () => _save(demo: true), child: const Text('Add a demo polyhouse (no hardware)')),
      ]),
    );
  }
}
