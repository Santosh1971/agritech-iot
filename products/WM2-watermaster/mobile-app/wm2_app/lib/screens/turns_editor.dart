import 'package:flutter/material.dart';

import '../services/farm.dart';
import '../services/node_store.dart';
import 'node_screen.dart' show showReply;

/// Device ID of a node: from its live state, else the one the app learned.
String nodeDev(FarmController farm, NodeEntry n) => '${farm.ctl(n)?.state?['dev'] ?? n.agId}';

/// The node leading zone turns among `nodes` (its state says role leader).
NodeEntry? zoneLeader(FarmController farm, List<NodeEntry> nodes) {
  for (final n in nodes) {
    if (farm.ctl(n)?.state?['zone']?['role'] == 'leader') return n;
  }
  return null;
}

/// Turn number (1-based) of valve `v` of node `dev` in any leader's turns, or null.
int? turnOf(FarmController farm, List<NodeEntry> nodes, String dev, int v) {
  final l = zoneLeader(farm, nodes);
  if (l == null) return null;
  final turns = (farm.ctl(l)?.state?['zone']?['turns'] as List?) ?? [];
  for (var t = 0; t < turns.length; t++) {
    for (final e in (turns[t] as List)) {
      if (e['id'] == '$dev:$v') return t + 1;
    }
  }
  return null;
}

/// One line for the zone card: "Turn 2 of 3 · water on".
String? turnsSummary(FarmController farm, List<NodeEntry> nodes) {
  final l = zoneLeader(farm, nodes);
  if (l == null) return null;
  final z = farm.ctl(l)!.state!['zone'] as Map;
  final n = (z['turns'] as List?)?.length ?? 0;
  final turn = (z['turn'] as num?)?.toInt() ?? 0;
  final water = z['water'] == true ? 'water on' : 'no water';
  return turn == 0 ? 'Turns: all $n done today' : 'Turn $turn of $n · $water';
}

/// Editor for the zone's turns. Saved on the node with the pressure switch
/// (the leader), which then runs the turns itself over the farm WiFi.
Future<void> showTurnsEditor(BuildContext context, FarmController farm, List<NodeEntry> nodes) async {
  final online = nodes.where((n) => farm.ctl(n)?.state != null).toList();
  final withPs = online.where((n) => farm.ctl(n)!.state!['ps_fitted'] == true).toList();
  if (withPs.isEmpty) {
    await showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Zone turns'),
        content: const Text('Turns are run by the node with the pressure switch. None of this zone\'s connected nodes '
            'has one (Settings > Pressure switch fitted), or that node is not connected right now.'),
        actions: [TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('OK'))],
      ),
    );
    return;
  }
  var leader = zoneLeader(farm, online) ?? withPs.first;

  // Every valve of the zone's nodes that has a known device ID.
  final labels = <String, String>{};
  for (final n in nodes) {
    final dev = nodeDev(farm, n);
    if (dev.isEmpty) continue;
    for (final v in [1, 2]) {
      labels['$dev:$v'] = '${n.name} V$v';
    }
  }
  // Start from the leader's current turns.
  final turns = <List<String>>[
    for (final t in ((farm.ctl(leader)!.state!['zone']?['turns'] as List?) ?? []))
      [for (final e in (t as List)) '${e['id']}']
  ];
  if (turns.isEmpty) turns.add([]);

  final save = await showDialog<bool>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setD) {
        final used = turns.expand((t) => t).toSet();
        final free = labels.keys.where((k) => !used.contains(k)).toList();
        return AlertDialog(
          title: const Text('Zone turns'),
          content: SizedBox(
            width: 420,
            child: SingleChildScrollView(
              child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Text(
                    'When the pressure switch shows water, only the valves of the current turn run their programs. '
                    'The next turn starts when every valve of the turn has finished today. Valves not listed run as before.',
                    style: TextStyle(fontSize: 12)),
                if (withPs.length > 1)
                  DropdownButtonFormField<NodeEntry>(
                    value: leader,
                    decoration: const InputDecoration(labelText: 'Pressure switch node (leader)'),
                    items: [for (final n in withPs) DropdownMenuItem(value: n, child: Text(n.name))],
                    onChanged: (v) => setD(() => leader = v!),
                  )
                else
                  Padding(
                    padding: const EdgeInsets.only(top: 8),
                    child: Text('Pressure switch node: ${leader.name}', style: const TextStyle(fontWeight: FontWeight.w600)),
                  ),
                const SizedBox(height: 8),
                for (var t = 0; t < turns.length; t++)
                  Card(
                    margin: const EdgeInsets.symmetric(vertical: 4),
                    child: Padding(
                      padding: const EdgeInsets.all(8),
                      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                        Row(children: [
                          Text('Turn ${t + 1}', style: const TextStyle(fontWeight: FontWeight.bold)),
                          const Spacer(),
                          if (turns.length > 1)
                            IconButton(
                              icon: const Icon(Icons.delete_outline, size: 20),
                              tooltip: 'Remove turn',
                              onPressed: () => setD(() => turns.removeAt(t)),
                            ),
                        ]),
                        Wrap(spacing: 6, runSpacing: 4, children: [
                          for (final id in turns[t])
                            InputChip(
                              label: Text(labels[id] ?? id),
                              onDeleted: () => setD(() => turns[t].remove(id)),
                            ),
                          if (free.isNotEmpty)
                            PopupMenuButton<String>(
                              tooltip: 'Add a valve',
                              itemBuilder: (_) => [for (final k in free) PopupMenuItem(value: k, child: Text(labels[k]!))],
                              onSelected: (k) => setD(() => turns[t].add(k)),
                              child: const Chip(avatar: Icon(Icons.add, size: 16), label: Text('valve')),
                            ),
                        ]),
                      ]),
                    ),
                  ),
                TextButton.icon(
                  icon: const Icon(Icons.add),
                  label: const Text('Add turn'),
                  onPressed: turns.length >= 8 ? null : () => setD(() => turns.add([])),
                ),
              ]),
            ),
          ),
          actions: [
            TextButton(onPressed: () => Navigator.pop(ctx, null), child: const Text('Cancel')),
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Turn off')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Save')),
          ],
        );
      },
    ),
  );
  if (save == null || !context.mounted) return;
  final list = save ? turns.where((t) => t.isNotEmpty).toList() : <List<String>>[];
  // Only one leader per zone: switch off any other node that was leading.
  for (final n in online) {
    if (n != leader && farm.ctl(n)!.state!['zone']?['role'] == 'leader') {
      await farm.ctl(n)!.send({'cmd': 'set_seq', 'turns': []});
    }
  }
  final r = await farm.ctl(leader)!.send({'cmd': 'set_seq', 'turns': list});
  if (context.mounted) {
    showReply(context, r, list.isEmpty ? 'Zone turns off' : 'Saved ${list.length} turn${list.length == 1 ? '' : 's'} on ${leader.name}');
  }
}
