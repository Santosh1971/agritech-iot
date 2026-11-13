import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';

/// Paired sensor nodes, which ones drive misting, and adding new ones.
class SensorsTab extends StatelessWidget {
  const SensorsTab({super.key, required this.conn});
  final HouseConn conn;

  String _label(Map<String, dynamic> n) => (n['name'] as String?)?.isNotEmpty == true ? n['name'] : n['id'];

  @override
  Widget build(BuildContext context) {
    final s = St(conn.status!);
    final cs = Theme.of(context).colorScheme;
    final nodes = s.nodes;
    final chosen = [for (final n in nodes) if (n['control'] == true) n['mac'] as String];
    final local = s.localControl;

    String summary;
    final k = chosen.length + (local ? 1 : 0);
    if (k == 0) {
      summary = 'No sensor chosen — misting uses the outside sensor at the pump panel.';
    } else if (k == 1) {
      summary = 'Misting follows one sensor: ${local ? 'Outside' : _label(nodes.firstWhere((n) => n['mac'] == chosen.first))}.';
    } else if (chosen.length == nodes.length && nodes.isNotEmpty && !local) {
      summary = 'Misting follows the average of all ${nodes.length} sensors.';
    } else {
      summary = 'Misting follows the average of $k chosen sensors.';
    }

    void setSources(List<String> macs, bool useLocal) {
      runCmd(context, conn.send({'cmd': 'set_control', 'sources': [...macs, if (useLocal) 'local']}));
    }

    return ListView(padding: const EdgeInsets.only(bottom: 32), children: [
      Card(
        margin: const EdgeInsets.fromLTRB(12, 12, 12, 0),
        color: cs.primaryContainer,
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const Text('Control input', style: TextStyle(fontWeight: FontWeight.w600)),
            const SizedBox(height: 4),
            Text(summary),
            const SizedBox(height: 8),
            Wrap(spacing: 8, children: [
              if (nodes.length > 1)
                ActionChip(
                  avatar: const Icon(Icons.select_all, size: 18),
                  label: const Text('Average of all sensors'),
                  onPressed: () => setSources([for (final n in nodes) n['mac'] as String], false),
                ),
            ]),
            Text('Tick "Use for control" on one sensor, or on several to average them.',
                style: TextStyle(fontSize: 12, color: cs.onPrimaryContainer.withValues(alpha: 0.8))),
          ]),
        ),
      ),

      SectionTitle('Sensors in this greenhouse (${nodes.length})'),
      if (nodes.isEmpty)
        const Padding(
          padding: EdgeInsets.symmetric(horizontal: 16),
          child: Text('No sensors added yet. Power on a sensor node — it appears under "New sensors found" '
              'within about 10 seconds.'),
        ),
      for (final n in nodes)
        _NodeCard(
          node: n,
          label: _label(n),
          onControl: (v) {
            final m = n['mac'] as String;
            setSources(v ? [...chosen, m] : chosen.where((x) => x != m).toList(), local);
          },
          onRelay: n['relay'] == null
              ? null
              : () => runCmd(context, conn.send({'cmd': 'node_relay', 'mac': n['mac'], 'on': n['relay'] != true}),
                  okMsg: n['relay'] == true ? 'It will no longer relay for other sensors' : 'It may now relay for other sensors'),
          onIdentify: () => runCmd(context, conn.send({'cmd': 'node_identify', 'mac': n['mac']}),
              okMsg: 'The sensor\'s lights will flash for 15 s'),
          onRename: () => _rename(context, n),
          onRemove: () => _remove(context, n),
        ),

      // Controller's own sensor
      Card(
        margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 8, 8),
          child: Row(children: [
            const Icon(Icons.wb_sunny_outlined),
            const SizedBox(width: 12),
            Expanded(
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Text('Outside (pump panel)', style: TextStyle(fontWeight: FontWeight.w600)),
                Text('${fmtT(s.outsideT)}  ${fmtRh(s.outsideRh)}  ·  ${s.outsideSensor}',
                    style: TextStyle(color: cs.onSurfaceVariant)),
              ]),
            ),
            Column(children: [
              Switch(value: local, onChanged: (v) => setSources(chosen, v)),
              const Text('Use for control', style: TextStyle(fontSize: 11)),
            ]),
          ]),
        ),
      ),

      SectionTitle('New sensors found', trailing: const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))),
      if (s.discovered.isEmpty)
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Text('Looking for sensors… A new (or reset) sensor shows here while it is powered and within range.',
              style: TextStyle(color: cs.onSurfaceVariant)),
        ),
      for (final d in s.discovered)
        Card(
          margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: ListTile(
            leading: const Icon(Icons.fiber_new),
            title: Text(d['id'] ?? ''),
            subtitle: Row(children: [
              SignalBars(rssi: (d['rssi'] as num?)?.toInt() ?? 0),
              if (d['via'] != null) ...[
                const SizedBox(width: 8),
                Text('via ${d['via']}', style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
              ],
            ]),
            trailing: d['pending'] == true
                ? const Row(mainAxisSize: MainAxisSize.min, children: [
                    SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)),
                    SizedBox(width: 8),
                    Text('Adding…'),
                  ])
                : FilledButton(
                    onPressed: () => runCmd(context, conn.send({'cmd': 'node_add', 'mac': d['mac']}),
                        okMsg: 'Adding — takes up to 10 s'),
                    child: const Text('Add')),
          ),
        ),
    ]);
  }

  Future<void> _rename(BuildContext context, Map<String, dynamic> n) async {
    final c = TextEditingController(text: n['name'] ?? '');
    final name = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Name for ${n['id']}'),
        content: TextField(
            controller: c, autofocus: true, maxLength: 19, decoration: const InputDecoration(hintText: 'e.g. Centre, North end')),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, c.text.trim()), child: const Text('Save')),
        ],
      ),
    );
    if (name != null && context.mounted) runCmd(context, conn.send({'cmd': 'node_rename', 'mac': n['mac'], 'name': name}));
  }

  Future<void> _remove(BuildContext context, Map<String, dynamic> n) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Remove ${_label(n)}?'),
        content: const Text('The sensor is released and can be added again, here or to another greenhouse.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Remove')),
        ],
      ),
    );
    if (ok == true && context.mounted) runCmd(context, conn.send({'cmd': 'node_remove', 'mac': n['mac']}));
  }
}

class _NodeCard extends StatelessWidget {
  const _NodeCard(
      {required this.node,
      required this.label,
      required this.onControl,
      required this.onIdentify,
      required this.onRename,
      required this.onRemove,
      this.onRelay});
  final Map<String, dynamic> node;
  final String label;
  final ValueChanged<bool> onControl;
  final VoidCallback onIdentify, onRename, onRemove;
  final VoidCallback? onRelay; // repeater-version sensors only

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final online = node['online'] == true;
    final reset = node['reset'] == true;
    final rx = (node['rx'] as num?)?.toInt() ?? 0;
    final lost = (node['lost'] as num?)?.toInt() ?? 0;
    final delivered = rx + lost == 0 ? null : (100 * rx / (rx + lost));
    final age = (node['age'] as num?)?.toInt();
    // Repeater version: "via FG1N_xxxx" when it reports through another sensor
    // (its signal is then the far hop), and how many far sensors it carries.
    final via = node['via'] as String?;
    final carrying = (node['carrying'] as num?)?.toInt() ?? 0;
    final path = [
      if (via != null) 'via $via',
      if (node['relay'] == true && carrying > 0) 'relaying for $carrying',
      if (node['relay'] == false) 'relay off',
    ].join(' · ');
    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 10, 4, 10),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Icon(online ? Icons.sensors : Icons.sensors_off, color: online ? cs.primary : Colors.grey),
            const SizedBox(width: 10),
            Expanded(
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text(label, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 16)),
                Text(path.isEmpty ? (node['id'] ?? '') : '${node['id'] ?? ''} · $path',
                    style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
              ]),
            ),
            if (online && node['ok'] == true)
              Text('${fmtT((node['t'] as num?)?.toDouble())}\n${fmtRh((node['rh'] as num?)?.toDouble())}',
                  textAlign: TextAlign.right, style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600))
            else
              Text(reset ? 'reset — add again' : online ? 'sensor fault' : 'offline',
                  style: const TextStyle(color: Colors.red)),
            PopupMenuButton<String>(
              onSelected: (v) => {'identify': onIdentify, 'rename': onRename, 'remove': onRemove, 'relay': onRelay}[v]?.call(),
              itemBuilder: (_) => [
                const PopupMenuItem(value: 'identify', child: Text('Flash its lights')),
                const PopupMenuItem(value: 'rename', child: Text('Rename')),
                if (onRelay != null)
                  PopupMenuItem(
                      value: 'relay',
                      child: Text(node['relay'] == true ? 'Stop relaying for other sensors' : 'Allow relaying for other sensors')),
                const PopupMenuItem(value: 'remove', child: Text('Remove')),
              ],
            ),
          ]),
          const SizedBox(height: 6),
          Row(children: [
            SignalBars(rssi: (node['rssi'] as num?)?.toInt() ?? 0),
            const SizedBox(width: 12),
            if (delivered != null)
              Text('${delivered.toStringAsFixed(delivered >= 99.5 ? 0 : 1)} % delivered',
                  style: TextStyle(fontSize: 11, color: delivered < 90 ? Colors.orange : cs.onSurfaceVariant)),
            const SizedBox(width: 12),
            if (age != null) Text('${age}s ago', style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant)),
            const Spacer(),
            const Text('Use for control', style: TextStyle(fontSize: 12)),
            Checkbox(value: node['control'] == true, onChanged: (v) => onControl(v ?? false)),
          ]),
        ]),
      ),
    );
  }
}
