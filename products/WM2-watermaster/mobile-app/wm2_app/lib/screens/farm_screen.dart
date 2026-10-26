import 'package:flutter/material.dart';

import '../services/farm.dart';
import '../services/node_store.dart';
import 'node_screen.dart';
import 'valve_sheet.dart';

const _dayNames = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

String fmtWhen(int ts) {
  if (ts <= 0) return '-';
  final d = DateTime.fromMillisecondsSinceEpoch(ts * 1000);
  String two(int v) => v.toString().padLeft(2, '0');
  return '${_dayNames[d.weekday - 1]} ${d.day} ${two(d.hour)}:${two(d.minute)}';
}

String fmtDue(int? ts) {
  if (ts == null) return 'no program';
  final now = DateTime.now();
  final d = DateTime.fromMillisecondsSinceEpoch(ts * 1000);
  if (d.isBefore(now.add(const Duration(minutes: 1)))) return 'now (when water comes)';
  String two(int v) => v.toString().padLeft(2, '0');
  final time = d.hour == 0 && d.minute == 0 ? '' : ' ${two(d.hour)}:${two(d.minute)}';
  final today = DateTime(now.year, now.month, now.day);
  final day = DateTime(d.year, d.month, d.day);
  final diff = day.difference(today).inDays;
  if (diff == 0) return 'today$time';
  if (diff == 1) return 'tomorrow$time';
  return '${_dayNames[d.weekday - 1]} ${d.day}$time';
}

String progText(Map? p) {
  if (p == null || p['enabled'] != true) return 'no program';
  final days = ((p['days'] as List?) ?? []).map((e) => '$e'[0].toUpperCase() + '$e'.substring(1)).join(' ');
  final every = p['every_days'] != null ? 'every ${p['every_days']} days' : days;
  final amt = p['type'] == 'litres' ? '${p['litres']} L' : '${p['minutes']} min';
  final w = (p['window'] as List?) ?? ['00:00', '23:59'];
  final win = (w[0] == '00:00' && w[1] == '23:59') ? '' : ' (${w[0]}-${w[1]})';
  return '$every, $amt/day$win';
}

class FarmScreen extends StatelessWidget {
  const FarmScreen({super.key, required this.farm, required this.store});
  final FarmController farm;
  final NodeStore store;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: farm,
      builder: (context, _) {
        final zones = store.layout();
        return Scaffold(
          appBar: AppBar(
            title: Row(children: [
              ClipRRect(
                  borderRadius: BorderRadius.circular(8),
                  child: Image.asset('assets/images/logo-mark.png', height: 36, width: 36)),
              const SizedBox(width: 10),
              const Text('Farm'),
            ]),
            actions: [IconButton(icon: const Icon(Icons.refresh), tooltip: 'Reconnect', onPressed: farm.reconnectAll)],
          ),
          body: store.nodes.isEmpty
              ? const Center(
                  child: Padding(
                      padding: EdgeInsets.all(24),
                      child: Text('No nodes yet. Add them on the Nodes tab, with their zone and position.')))
              : ListView(padding: const EdgeInsets.all(10), children: [
                  _Banner(farm: farm, store: store),
                  for (final z in zones) _ZoneCard(farm: farm, store: store, zone: z.key, nodes: z.value),
                  Padding(
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    child: OutlinedButton.icon(
                      icon: const Icon(Icons.add),
                      label: const Text('Add zone'),
                      onPressed: () => addZone(context, store),
                    ),
                  ),
                ]),
        );
      },
    );
  }
}

// ------------------------------------------------------------------ banner

class _Banner extends StatelessWidget {
  const _Banner({required this.farm, required this.store});
  final FarmController farm;
  final NodeStore store;

  String _where(ValveRef r) => '${store.zone(r.node.zone).name} · ${r.node.name} · V${r.n}';

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    final cs = Theme.of(context).colorScheme;
    final all = farm.valves();
    final open = all.where((r) => r.open).toList();
    final closed = all.where((r) => !r.open && r.lastEnd > 0).toList()..sort((a, b) => b.lastEnd.compareTo(a.lastEnd));
    final due = all.where((r) => !r.open && r.nextDue != null).toList()..sort((a, b) => a.nextDue!.compareTo(b.nextDue!));
    final connected = store.nodes.where((n) => farm.ctl(n)?.connected ?? false).length;
    final wet = <String>[];
    for (final n in store.nodes) {
      if (farm.ctl(n)?.state?['water'] == true) wet.add('${store.zone(n.zone).name} ${n.name}');
    }

    return Card(
      color: open.isNotEmpty ? cs.primaryContainer : null,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          if (open.isEmpty)
            Text('Nothing irrigating now', style: t.titleMedium?.copyWith(fontWeight: FontWeight.bold))
          else
            Text('Irrigating now', style: t.titleMedium?.copyWith(fontWeight: FontWeight.bold)),
          for (final r in open) ...[
            const SizedBox(height: 6),
            Text(_where(r), style: t.titleSmall),
            Text('since ${fmtWhen((r.v['run']?['since'] as num?)?.toInt() ?? 0)}  ·  '
                '${r.v['run']?['min'] ?? r.v['run_min'] ?? 0} of ${r.v['planned_min'] ?? '-'} min  ·  ${r.v['run']?['l'] ?? '-'} L'),
            Text('program: ${progText(r.v['prog'])}', style: t.bodySmall),
          ],
          if (open.isEmpty && closed.isNotEmpty)
            Text('Last: ${_where(closed.first)} ended ${fmtWhen(closed.first.lastEnd)} '
                '(${closed.first.v['last']['min']} min, ${closed.first.v['last']['l']} L)'),
          const Divider(height: 18),
          Row(children: [
            Icon(Icons.water_drop, size: 18, color: wet.isEmpty ? null : Colors.blue),
            const SizedBox(width: 6),
            Expanded(child: Text(wet.isEmpty ? 'No water in any pipe' : 'Water in pipe: ${wet.join(', ')}')),
          ]),
          const SizedBox(height: 4),
          Row(children: [
            const Icon(Icons.schedule, size: 18),
            const SizedBox(width: 6),
            Expanded(
                child: Text(due.isEmpty
                    ? 'Nothing due'
                    : 'Next due: ${_where(due.first)}, ${fmtDue(due.first.nextDue)}  ·  ${progText(due.first.v['prog'])}')),
          ]),
          const SizedBox(height: 4),
          Text('$connected of ${store.nodes.length} nodes connected', style: t.bodySmall),
        ]),
      ),
    );
  }
}

// ------------------------------------------------------------------ zone

class _ZoneCard extends StatelessWidget {
  const _ZoneCard({required this.farm, required this.store, required this.zone, required this.nodes});
  final FarmController farm;
  final NodeStore store;
  final ZoneInfo zone;
  final List<NodeEntry> nodes;

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    // The most recently finished valve in this zone gets the "last irrigated" mark.
    ValveRef? last;
    for (final r in farm.valves()) {
      if (r.node.zone != zone.key || r.open || r.lastEnd == 0) continue;
      if (last == null || r.lastEnd > last.lastEnd) last = r;
    }
    final water = nodes.any((n) => farm.ctl(n)?.state?['water'] == true);
    final layoutName = {'center': 'pipe in the centre', 'left': 'pipe on the left', 'right': 'pipe on the right'}[zone.layout];

    Widget block(NodeEntry n, int v, bool toLeft) => _ValveBlock(
          farm: farm,
          store: store,
          node: n,
          valve: v,
          toLeft: toLeft,
          isLast: last != null && last.node == n && last.n == v,
        );

    Widget hub(NodeEntry n) => _NodeHub(farm: farm, store: store, node: n);

    final pipeColor = water ? Colors.blue : Colors.grey.shade400;
    Widget body;
    if (zone.layout == 'center') {
      body = Stack(children: [
        Positioned.fill(child: Center(child: Container(width: 6, color: pipeColor))),
        Column(children: [
          for (final n in nodes)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 6),
              child: Row(children: [
                Expanded(child: block(n, 1, true)),
                hub(n),
                Expanded(child: block(n, 2, false)),
              ]),
            ),
        ]),
      ]);
    } else {
      final left = zone.layout == 'left';
      body = Stack(children: [
        Positioned(top: 0, bottom: 0, left: left ? 30 : null, right: left ? null : 30, child: Container(width: 6, color: pipeColor)),
        Column(children: [
          for (final n in nodes)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 6),
              child: Row(children: [
                if (left) hub(n),
                Expanded(
                  child: Column(children: [
                    block(n, 1, !left),
                    const SizedBox(height: 6),
                    block(n, 2, !left),
                  ]),
                ),
                if (!left) hub(n),
              ]),
            ),
        ]),
      ]);
    }

    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(10, 8, 10, 10),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Text(zone.name, style: t.titleMedium?.copyWith(fontWeight: FontWeight.bold)),
            const SizedBox(width: 8),
            Expanded(child: Text('$layoutName · ${nodes.length} node${nodes.length == 1 ? '' : 's'}', style: t.bodySmall)),
            IconButton(icon: const Icon(Icons.edit_outlined, size: 20), tooltip: 'Rename zone', onPressed: () => _editZone(context)),
          ]),
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: SegmentedButton<String>(
              showSelectedIcon: false,
              style: const ButtonStyle(visualDensity: VisualDensity.compact),
              segments: const [
                ButtonSegment(value: 'left', icon: Icon(Icons.align_horizontal_left, size: 18), label: Text('Left')),
                ButtonSegment(value: 'center', icon: Icon(Icons.align_horizontal_center, size: 18), label: Text('Centre')),
                ButtonSegment(value: 'right', icon: Icon(Icons.align_horizontal_right, size: 18), label: Text('Right')),
              ],
              selected: {zone.layout},
              onSelectionChanged: (v) {
                zone.layout = v.first;
                store.save();
              },
            ),
          ),
          if (nodes.isEmpty)
            Padding(
              padding: const EdgeInsets.all(8),
              child: Text('No nodes in this zone yet. On the Nodes tab, add or edit a node and pick ${zone.name}.',
                  style: t.bodySmall),
            )
          else
            body,
        ]),
      ),
    );
  }

  Future<void> _editZone(BuildContext context) async {
    final name = TextEditingController(text: zone.name);
    String layout = zone.layout;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setD) => AlertDialog(
          title: Text('Zone ${zone.key}'),
          content: Column(mainAxisSize: MainAxisSize.min, children: [
            TextField(controller: name, decoration: const InputDecoration(labelText: 'Name')),
            const SizedBox(height: 12),
            RadioListTile<String>(
              value: 'center',
              groupValue: layout,
              onChanged: (v) => setD(() => layout = v!),
              title: const Text('Main pipe in the centre'),
              subtitle: const Text('V1 with FL1/FL2 on one side, V2 with FL3/FL4 on the other'),
            ),
            RadioListTile<String>(
              value: 'left',
              groupValue: layout,
              onChanged: (v) => setD(() => layout = v!),
              title: const Text('Main pipe on the left'),
              subtitle: const Text('V1 and V2 both go to the right'),
            ),
            RadioListTile<String>(
              value: 'right',
              groupValue: layout,
              onChanged: (v) => setD(() => layout = v!),
              title: const Text('Main pipe on the right'),
              subtitle: const Text('V1 and V2 both go to the left'),
            ),
          ]),
          actions: [
            if (nodes.isEmpty)
              TextButton(
                onPressed: () {
                  store.zones.remove(zone.key);
                  store.save();
                  Navigator.pop(ctx, false);
                },
                child: const Text('Delete zone'),
              ),
            TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
            FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Save')),
          ],
        ),
      ),
    );
    if (ok != true) return;
    zone
      ..name = name.text.trim().isEmpty ? zone.name : name.text.trim()
      ..layout = layout;
    await store.save();
  }
}

// ------------------------------------------------------------------ node + valve

class _NodeHub extends StatelessWidget {
  const _NodeHub({required this.farm, required this.store, required this.node});
  final FarmController farm;
  final NodeStore store;
  final NodeEntry node;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final c = farm.ctl(node);
    final s = c?.state;
    final online = c?.connected ?? false;
    final water = s?['water'] == true;
    final batt = (s?['batt']?['v'] as num?)?.toDouble() ?? 0;
    return GestureDetector(
      onTap: () => Navigator.push(
          context, MaterialPageRoute(builder: (_) => NodeScreen(entry: node, store: store, shared: c))),
      child: Container(
        width: 60,
        margin: const EdgeInsets.symmetric(horizontal: 4),
        padding: const EdgeInsets.symmetric(vertical: 8),
        decoration: BoxDecoration(
          color: online ? cs.surface : cs.surfaceContainerHighest,
          border: Border.all(color: water ? Colors.blue : cs.outline, width: water ? 2.5 : 1.2),
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(children: [
          Text('N${node.position}', style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
          Icon(online ? (water ? Icons.water_drop : Icons.water_drop_outlined) : Icons.cloud_off,
              size: 18, color: online ? (water ? Colors.blue : null) : cs.error),
          Text(online ? (batt > 0 ? '${batt.toStringAsFixed(1)}V' : 'USB') : 'offline', style: const TextStyle(fontSize: 10)),
        ]),
      ),
    );
  }
}

class _ValveBlock extends StatelessWidget {
  const _ValveBlock(
      {required this.farm,
      required this.store,
      required this.node,
      required this.valve,
      required this.toLeft,
      required this.isLast});
  final FarmController farm;
  final NodeStore store;
  final NodeEntry node;
  final int valve;
  final bool toLeft; // laterals run to the left of the valve
  final bool isLast;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final s = farm.ctl(node)?.state;
    final vl = (s?['valves'] as List?) ?? [];
    final v = vl.length >= valve ? Map<String, dynamic>.from(vl[valve - 1]) : <String, dynamic>{};
    final flows = ((s?['flow'] as List?) ?? []).map((e) => Map<String, dynamic>.from(e)).where((f) => f['valve'] == valve).toList();
    final open = v['open'] == true;
    final flowing = open && (s?['water'] == true || s?['bypass']?['on'] == true);
    final last = v['last'] as Map?;

    String status;
    Color dot;
    if (s == null) {
      status = 'offline';
      dot = cs.outline;
    } else if (v['pending'] == true) {
      status = 'pending 12V';
      dot = Colors.orange;
    } else if (open) {
      status = '${v['run']?['min'] ?? v['run_min'] ?? 0}/${v['planned_min'] ?? '-'} min';
      dot = Colors.green;
    } else if (v['waiting'] == true) {
      status = 'waiting water';
      dot = Colors.orange;
    } else if (v['mode'] == 'off') {
      status = 'off';
      dot = cs.outline;
    } else {
      status = 'closed';
      dot = cs.outline;
    }

    final border = open
        ? Border.all(color: Colors.green, width: 3)
        : isLast
            ? Border.all(color: Colors.amber.shade700, width: 2.5)
            : Border.all(color: cs.outlineVariant);

    final small = Theme.of(context).textTheme.bodySmall?.copyWith(fontSize: 11);
    final laterals = Column(crossAxisAlignment: toLeft ? CrossAxisAlignment.end : CrossAxisAlignment.start, children: [
      for (var i = 0; i < flows.length; i++)
        Row(mainAxisSize: MainAxisSize.min, children: [
          if (!toLeft) Text('FL${flows[i]['in']} ', style: small),
          _Laterals(active: flowing, width: 34),
          if (toLeft) Text(' FL${flows[i]['in']}', style: small),
        ]),
    ]);

    return GestureDetector(
      onTap: s == null
          ? null
          : () => showValveSheet(context, store: store, node: node, ctl: farm.ctl(node)!, valve: valve),
      child: Container(
        padding: const EdgeInsets.all(6),
        decoration: BoxDecoration(
          border: border,
          borderRadius: BorderRadius.circular(10),
          color: open ? Colors.green.withValues(alpha: 0.08) : Theme.of(context).cardColor,
        ),
        child: Column(crossAxisAlignment: toLeft ? CrossAxisAlignment.end : CrossAxisAlignment.start, children: [
          Row(mainAxisAlignment: toLeft ? MainAxisAlignment.end : MainAxisAlignment.start, children: [
            Icon(Icons.circle, size: 10, color: dot),
            const SizedBox(width: 4),
            Text('V$valve', style: const TextStyle(fontWeight: FontWeight.bold)),
            const SizedBox(width: 4),
            Flexible(child: Text(status, style: small, overflow: TextOverflow.ellipsis)),
          ]),
          laterals,
          if (isLast && !open) Text('last irrigated', style: small?.copyWith(color: Colors.amber.shade800, fontWeight: FontWeight.bold)),
          Text(last == null ? 'last: never' : 'last ${fmtWhen((last['end'] as num).toInt())}', style: small, textAlign: toLeft ? TextAlign.right : TextAlign.left),
          if (last != null) Text('${last['min']} min · ${last['l']} L', style: small),
          Text(open ? 'running now' : 'next ${fmtDue((v['next_due'] as num?)?.toInt())}',
              style: small, textAlign: toLeft ? TextAlign.right : TextAlign.left),
        ]),
      ),
    );
  }
}

/// Three drip lines (one flow sensor covers 3 laterals).
class _Laterals extends StatelessWidget {
  const _Laterals({required this.active, required this.width});
  final bool active;
  final double width;
  @override
  Widget build(BuildContext context) {
    final c = active ? Colors.blue : Theme.of(context).colorScheme.outline;
    return SizedBox(
      width: width,
      height: 14,
      child: Column(mainAxisAlignment: MainAxisAlignment.spaceEvenly, children: [
        for (var i = 0; i < 3; i++) Container(height: 2, color: c),
      ]),
    );
  }
}


/// Creates the next zone (z1, z2, ...) with a name and layout.
Future<String?> addZone(BuildContext context, NodeStore store) async {
  final key = store.nextZoneKey();
  final name = TextEditingController(text: 'Zone ${key.substring(1)}');
  String layout = 'center';
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, setD) => AlertDialog(
        title: const Text('Add zone'),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(controller: name, decoration: const InputDecoration(labelText: 'Name')),
          const SizedBox(height: 12),
          for (final o in const [
            ['center', 'Main pipe in the centre', 'V1 one side, V2 the other'],
            ['left', 'Main pipe on the left', 'V1 and V2 both to the right'],
            ['right', 'Main pipe on the right', 'V1 and V2 both to the left'],
          ])
            RadioListTile<String>(
              value: o[0],
              groupValue: layout,
              onChanged: (v) => setD(() => layout = v!),
              title: Text(o[1]),
              subtitle: Text(o[2]),
            ),
        ]),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Add')),
        ],
      ),
    ),
  );
  if (ok != true) return null;
  store.zones[key] = ZoneInfo(key: key, name: name.text.trim().isEmpty ? 'Zone ${key.substring(1)}' : name.text.trim(), layout: layout);
  await store.save();
  return key;
}
