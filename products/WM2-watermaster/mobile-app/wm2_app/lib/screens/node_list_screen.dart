import 'package:flutter/material.dart';

import '../services/mqtt_link.dart';
import '../services/node_store.dart';
import 'node_screen.dart';

class NodeListScreen extends StatelessWidget {
  const NodeListScreen({super.key, required this.store});
  final NodeStore store;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: store,
      builder: (context, _) => Scaffold(
        appBar: AppBar(
          title: Row(children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: Image.asset('assets/images/logo-mark.png', height: 36, width: 36),
            ),
            const SizedBox(width: 10),
            const Text('ASAC-WM2'),
          ]),
          actions: [
            IconButton(
              tooltip: 'MQTT brokers',
              icon: const Icon(Icons.cloud_outlined),
              onPressed: () => _editBrokers(context),
            ),
          ],
        ),
        body: store.nodes.isEmpty
            ? ListView(padding: const EdgeInsets.all(24), children: [
                Center(
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(16),
                    child: Image.asset('assets/images/logo-full.png', width: 220),
                  ),
                ),
                const SizedBox(height: 24),
                const Text(
                    'No nodes yet. Tap + to add one.\n\n'
                    'On the farm WiFi, use the node\'s IP address (from the router or the node\'s serial log).\n'
                    'Connected to the node\'s own WiFi (WM2-z1-n1)? Use 192.168.4.1.\n'
                    'Away from the farm, the app goes through the node\'s MQTT broker (AgriSense or Girish). '
                    'Set the broker logins with the cloud icon.'),
              ])
            : ListView(
                children: [
                  for (final n in store.nodes)
                    ListTile(
                      leading: const Icon(Icons.water_drop_outlined),
                      title: Text(n.name),
                      subtitle: Text([
                        if (n.host.isNotEmpty) n.host,
                        '${n.broker == 'girish' ? 'Girish' : 'AgriSense'}: ${n.broker == 'agrisense' && n.agId.isEmpty ? 'ID not known yet' : n.base}',
                      ].join('  |  ')),
                      trailing: PopupMenuButton<String>(
                        onSelected: (v) async {
                          if (v == 'edit') {
                            await _editNode(context, n);
                          } else {
                            store.nodes.remove(n);
                            await store.save();
                          }
                        },
                        itemBuilder: (_) => const [
                          PopupMenuItem(value: 'edit', child: Text('Edit')),
                          PopupMenuItem(value: 'delete', child: Text('Remove')),
                        ],
                      ),
                      onTap: () => Navigator.push(
                          context, MaterialPageRoute(builder: (_) => NodeScreen(entry: n, store: store))),
                    ),
                ],
              ),
        floatingActionButton: FloatingActionButton.extended(
          onPressed: () => _editNode(context, null),
          icon: const Icon(Icons.add),
          label: const Text('Add node'),
        ),
      ),
    );
  }

  Future<void> _editNode(BuildContext context, NodeEntry? existing) async {
    final n = existing ?? NodeEntry(name: 'Zone 1 Node ${store.nodes.length + 1}', node: 'n${store.nodes.length + 1}');
    final name = TextEditingController(text: n.name);
    final host = TextEditingController(text: n.host.isEmpty && existing == null ? '192.168.4.1' : n.host);
    final agId = TextEditingController(text: n.agId);
    final farm = TextEditingController(text: n.farm);
    final zone = TextEditingController(text: n.zone);
    final node = TextEditingController(text: n.node);
    String broker = n.broker;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setD) => AlertDialog(
          title: Text(existing == null ? 'Add node' : 'Edit node'),
          content: SingleChildScrollView(
            child: Column(mainAxisSize: MainAxisSize.min, children: [
              TextField(controller: name, decoration: const InputDecoration(labelText: 'Name')),
              TextField(
                  controller: host,
                  decoration: const InputDecoration(
                      labelText: 'Local address', helperText: 'Node IP, or 192.168.4.1 on its SoftAP. Empty = MQTT only')),
              const SizedBox(height: 12),
              DropdownButtonFormField<String>(
                value: broker,
                decoration: const InputDecoration(labelText: 'MQTT broker (away from the farm)'),
                items: const [
                  DropdownMenuItem(value: 'agrisense', child: Text('AgriSense')),
                  DropdownMenuItem(value: 'girish', child: Text('Girish')),
                ],
                onChanged: (v) => setD(() => broker = v!),
              ),
              if (broker == 'agrisense')
                TextField(
                    controller: agId,
                    decoration: const InputDecoration(
                        labelText: 'Device ID', helperText: 'WM2_xxxxxxxx; filled in when you first connect locally'))
              else
                Row(children: [
                  Expanded(child: TextField(controller: farm, decoration: const InputDecoration(labelText: 'Farm'))),
                  const SizedBox(width: 8),
                  Expanded(child: TextField(controller: zone, decoration: const InputDecoration(labelText: 'Zone'))),
                  const SizedBox(width: 8),
                  Expanded(child: TextField(controller: node, decoration: const InputDecoration(labelText: 'Node'))),
                ]),
            ]),
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Save')),
          ],
        ),
      ),
    );
    if (ok != true) return;
    n
      ..name = name.text.trim()
      ..host = host.text.trim()
      ..broker = broker
      ..agId = agId.text.trim()
      ..farm = farm.text.trim()
      ..zone = zone.text.trim()
      ..node = node.text.trim();
    if (existing == null) store.nodes.add(n);
    await store.save();
  }

  Future<void> _editBrokers(BuildContext context) async {
    final f = <String, TextEditingController>{};
    void init(String p, BrokerSettings b) {
      f['${p}host'] = TextEditingController(text: b.host);
      f['${p}port'] = TextEditingController(text: '${b.port}');
      f['${p}user'] = TextEditingController(text: b.user);
      f['${p}pass'] = TextEditingController(text: b.pass);
    }

    init('a', store.agrisense);
    init('g', store.girish);
    List<Widget> fields(String p, String title) => [
          Align(alignment: Alignment.centerLeft, child: Text(title, style: Theme.of(context).textTheme.titleSmall)),
          TextField(controller: f['${p}host'], decoration: const InputDecoration(labelText: 'Host')),
          TextField(
              controller: f['${p}port'], decoration: const InputDecoration(labelText: 'Port'), keyboardType: TextInputType.number),
          TextField(controller: f['${p}user'], decoration: const InputDecoration(labelText: 'User')),
          TextField(controller: f['${p}pass'], decoration: const InputDecoration(labelText: 'Password'), obscureText: true),
          const SizedBox(height: 16),
        ];
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('MQTT brokers (app login)'),
        content: SingleChildScrollView(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            ...fields('a', 'AgriSense'),
            ...fields('g', 'Girish'),
          ]),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Save')),
        ],
      ),
    );
    if (ok != true) return;
    BrokerSettings read(String p) => BrokerSettings(
        host: f['${p}host']!.text.trim(),
        port: int.tryParse(f['${p}port']!.text) ?? 1883,
        user: f['${p}user']!.text.trim(),
        pass: f['${p}pass']!.text);
    store.agrisense = read('a');
    store.girish = read('g');
    await store.save();
  }
}
