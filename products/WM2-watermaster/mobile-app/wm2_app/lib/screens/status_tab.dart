import 'package:flutter/material.dart';

import '../services/node_store.dart';
import 'node_screen.dart';

class StatusTab extends StatelessWidget {
  const StatusTab({super.key, required this.ctl});
  final NodeController ctl;

  @override
  Widget build(BuildContext context) {
    final s = ctl.state;
    if (s == null) return const Center(child: Text('waiting for node state...'));
    final batt = Map<String, dynamic>.from(s['batt'] ?? {});
    final bypass = Map<String, dynamic>.from(s['bypass'] ?? {});
    final valves = (s['valves'] as List? ?? []).map((e) => Map<String, dynamic>.from(e)).toList();
    final flow = (s['flow'] as List? ?? []).map((e) => Map<String, dynamic>.from(e)).toList();
    final alarms = (s['alarms'] as List? ?? []).cast<dynamic>();
    final cs = Theme.of(context).colorScheme;
    final double bv = (batt['v'] ?? 0).toDouble();

    return RefreshIndicator(
      onRefresh: () async {
        final r = await ctl.send({'cmd': 'get_state'});
        if (r['ok'] == true && r['data'] is Map) ctl.state = Map<String, dynamic>.from(r['data']);
      },
      child: ListView(padding: const EdgeInsets.all(12), children: [
        Card(
          child: Padding(
            padding: const EdgeInsets.all(14),
            child: Row(children: [
              Expanded(
                child: _Big(
                  label: 'Water in pipe',
                  value: s['water'] == true ? 'YES' : 'no',
                  color: s['water'] == true ? cs.primary : null,
                  sub: 'pressure switch ${s['ps'] == true ? 'ON' : 'off'}',
                ),
              ),
              Expanded(
                child: _Big(
                  label: 'Battery',
                  value: bv > 0 ? '${bv.toStringAsFixed(2)} V' : 'no 12V',
                  sub: bv > 0
                      ? '${batt['soc']}%  ${batt['trend']}  ~${(batt['days'] ?? 0).toStringAsFixed(0)} days'
                      : 'running on USB',
                  color: batt['level'] == 'critical' || batt['level'] == 'save' ? cs.error : null,
                ),
              ),
            ]),
          ),
        ),
        Card(
          child: SwitchListTile(
            title: const Text('Bypass pressure interlock'),
            subtitle: Text(bypass['on'] == true
                ? 'ON, ${((bypass['left_s'] ?? 0) / 60).ceil()} min left: valves open without water'
                : 'off: valves open only with water in the pipe'),
            value: bypass['on'] == true,
            onChanged: (v) async {
              int minutes = 60;
              if (v) {
                final m = await _askMinutes(context, 'Bypass for how many minutes?', 60);
                if (m == null) return;
                minutes = m;
              }
              final r = await ctl.send({'cmd': 'bypass', 'on': v, 'minutes': minutes});
              if (context.mounted) showReply(context, r, v ? 'Bypass on for $minutes min' : 'Bypass off');
            },
          ),
        ),
        for (final v in valves) _ValveCard(ctl: ctl, v: v),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Flow', style: Theme.of(context).textTheme.titleMedium),
              const SizedBox(height: 6),
              Table(
                columnWidths: const {0: FlexColumnWidth(1.2)},
                children: [
                  const TableRow(children: [Text('Input'), Text('Valve'), Text('L/min'), Text('Litres today')]),
                  for (final f in flow)
                    TableRow(children: [
                      Text('Flow ${f['in']}'),
                      Text((f['valve'] ?? 0) == 0 ? '-' : 'V${f['valve']}'),
                      Text('${(f['lpm'] ?? 0).toStringAsFixed(1)}'),
                      Text('${(f['l_today'] ?? 0).toStringAsFixed(1)}'),
                    ]),
                ],
              ),
            ]),
          ),
        ),
        Card(
          child: ListTile(
            leading: Icon(alarms.isEmpty ? Icons.check_circle_outline : Icons.warning_amber, color: alarms.isEmpty ? null : cs.error),
            title: Text(alarms.isEmpty ? 'No alarms' : alarms.join(', ')),
            subtitle: Text('WiFi ${s['wifi']?['rssi'] ?? '-'} dBm  |  MQTT ${s['mqtt'] == true ? 'ok' : 'off'}  |  ${s['state']}  |  fw ${s['fw']}'),
          ),
        ),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Recent events', style: Theme.of(context).textTheme.titleMedium),
              if (ctl.events.isEmpty) const Text('none yet'),
              for (final e in ctl.events.take(20))
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 3),
                  child: Row(children: [
                    SizedBox(width: 110, child: Text(fmtTs(e['ts']), style: Theme.of(context).textTheme.bodySmall)),
                    Expanded(child: Text(eventText(e))),
                  ]),
                ),
            ]),
          ),
        ),
      ]),
    );
  }
}

class _Big extends StatelessWidget {
  const _Big({required this.label, required this.value, this.sub, this.color});
  final String label, value;
  final String? sub;
  final Color? color;
  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Text(label, style: t.bodySmall),
      Text(value, style: t.headlineSmall?.copyWith(fontWeight: FontWeight.bold, color: color)),
      if (sub != null) Text(sub!, style: t.bodySmall),
    ]);
  }
}

class _ValveCard extends StatelessWidget {
  const _ValveCard({required this.ctl, required this.v});
  final NodeController ctl;
  final Map<String, dynamic> v;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final n = v['v'];
    String st;
    Color? col;
    if (v['pending'] == true) {
      st = 'pulse pending (12V?)';
      col = Colors.orange;
    } else if (v['open'] == true) {
      st = 'OPEN';
      col = cs.primary;
    } else if (v['waiting'] == true) {
      st = 'waiting for water';
      col = Colors.orange;
    } else {
      st = 'closed';
    }
    String prog;
    if (v['mode'] == 'program') {
      prog = v['due'] == true
          ? 'today ${v['run_min']} / ${v['planned_min'] ?? v['planned_l']} ${v['planned_l'] != null ? 'L' : 'min'}'
          : v['done'] == true
              ? 'done for today'
              : 'not due today';
    } else {
      prog = v['mode'];
    }
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Text('Valve $n', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(width: 8),
            Chip(
              label: Text(st, style: TextStyle(color: col == null ? null : Colors.white)),
              backgroundColor: col,
              visualDensity: VisualDensity.compact,
            ),
          ]),
          Text('mode ${v['mode']}  |  $prog'
              '${v['manual'] == true ? '  |  manual ${(v['manual_left_s'] ?? 0) > 0 ? '${((v['manual_left_s'] ?? 0) / 60).ceil()} min left' : 'on'}' : ''}'),
          Text('litres today ${(v['litres'] ?? 0).toStringAsFixed(1)}'),
          const SizedBox(height: 8),
          Row(children: [
            FilledButton.icon(
              icon: const Icon(Icons.play_arrow),
              label: const Text('Open'),
              onPressed: () async {
                final m = await _askMinutes(context, 'Open valve $n for how many minutes?', 10);
                if (m == null) return;
                final r = await ctl.send({'cmd': 'valve_open', 'valve': n, 'minutes': m});
                if (context.mounted) {
                  showReply(context, r,
                      r['data']?['state'] == 'waiting_for_water' ? 'V$n will open when water arrives' : 'Opening V$n for $m min');
                }
              },
            ),
            const SizedBox(width: 8),
            OutlinedButton.icon(
              icon: const Icon(Icons.stop),
              label: const Text('Close'),
              onPressed: () async {
                final r = await ctl.send({'cmd': 'valve_close', 'valve': n});
                if (context.mounted) showReply(context, r, 'Closing V$n');
              },
            ),
          ]),
        ]),
      ),
    );
  }
}

Future<int?> _askMinutes(BuildContext context, String title, int initial) async {
  final c = TextEditingController(text: '$initial');
  return showDialog<int>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: Text(title),
      content: TextField(
          controller: c, keyboardType: TextInputType.number, decoration: const InputDecoration(suffixText: 'min')),
      actions: [
        TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
        FilledButton(
            onPressed: () {
              final v = int.tryParse(c.text);
              Navigator.pop(ctx, v != null && v > 0 ? v : null);
            },
            child: const Text('OK')),
      ],
    ),
  );
}
