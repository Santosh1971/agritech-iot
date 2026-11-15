import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../services/node_store.dart';
import 'farm_screen.dart';
import 'node_screen.dart';

Future<void> showValveSheet(BuildContext context,
    {required NodeStore store, required NodeEntry node, required NodeController ctl, required int valve}) {
  return showModalBottomSheet(
    context: context,
    isScrollControlled: true,
    showDragHandle: true,
    builder: (_) => DraggableScrollableSheet(
      expand: false,
      initialChildSize: 0.85,
      maxChildSize: 0.95,
      builder: (ctx, sc) => _ValveSheet(store: store, node: node, ctl: ctl, valve: valve, scroll: sc),
    ),
  );
}

class _ValveSheet extends StatefulWidget {
  const _ValveSheet({required this.store, required this.node, required this.ctl, required this.valve, required this.scroll});
  final NodeStore store;
  final NodeEntry node;
  final NodeController ctl;
  final int valve;
  final ScrollController scroll;
  @override
  State<_ValveSheet> createState() => _ValveSheetState();
}

class _ValveSheetState extends State<_ValveSheet> {
  String period = '7d';
  DateTimeRange? range;
  Map<String, dynamic>? data;
  bool loading = false;
  String? error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      loading = true;
      error = null;
    });
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    DateTime from, to = now;
    String res = 'day';
    switch (period) {
      case 'today':
        from = today;
        res = 'hour';
        break;
      case 'yday':
        from = today.subtract(const Duration(days: 1));
        to = today.subtract(const Duration(seconds: 1));
        res = 'hour';
        break;
      case '30d':
        from = today.subtract(const Duration(days: 29));
        break;
      case 'range':
        from = range!.start;
        to = range!.end.add(const Duration(hours: 23, minutes: 59));
        if (to.difference(from).inDays > 400) res = 'day';
        break;
      default:
        from = today.subtract(const Duration(days: 6));
    }
    final r = await widget.ctl.send({
      'cmd': 'get_history',
      'from': from.millisecondsSinceEpoch ~/ 1000,
      'to': to.millisecondsSinceEpoch ~/ 1000,
      'res': res
    }, timeout: const Duration(seconds: 30));
    if (!mounted) return;
    setState(() {
      loading = false;
      if (r['ok'] == true) {
        data = Map<String, dynamic>.from(r['data']);
      } else {
        error = '${r['err']}';
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    final s = widget.ctl.state;
    final vl = (s?['valves'] as List?) ?? [];
    final v = vl.length >= widget.valve ? Map<String, dynamic>.from(vl[widget.valve - 1]) : <String, dynamic>{};
    final last = v['last'] as Map?;
    final zone = widget.store.zone(widget.node.zone);
    final iL = widget.valve == 1 ? [1, 2] : [3, 4];
    final iM = widget.valve == 1 ? 5 : 6;
    final pts = ((data?['points'] as List?) ?? []).cast<List>();
    final rows = <({int ts, double l, int m})>[
      for (final p in pts)
        (ts: (p[0] as num).toInt(), l: ((p[iL[0]] as num) + (p[iL[1]] as num)).toDouble(), m: (p[iM] as num).toInt())
    ];
    final totL = rows.fold<double>(0, (a, r) => a + r.l);
    final totM = rows.fold<int>(0, (a, r) => a + r.m);
    final daysIrr = data?['res'] == 'day' ? rows.where((r) => r.m > 0).length : null;

    return ListView(controller: widget.scroll, padding: const EdgeInsets.fromLTRB(16, 0, 16, 24), children: [
      Text('${zone.name} · ${widget.node.name} · Valve ${widget.valve}', style: t.titleLarge),
      const SizedBox(height: 6),
      _kv('Now', v['open'] == true ? 'OPEN, ${v['run']?['min'] ?? v['run_min']} of ${v['planned_min']} min, ${v['run']?['l'] ?? '-'} L' : (v['waiting'] == true ? 'waiting for water' : 'closed')),
      _kv('Program', '${v['mode']} · ${progText(v['prog'])}'),
      _kv('Last run', last == null ? 'never' : '${fmtWhen((last['start'] as num).toInt())} to ${fmtWhen((last['end'] as num).toInt())} · ${last['min']} min · ${last['l']} L'),
      _kv('Next due', fmtDue((v['next_due'] as num?)?.toInt())),
      const SizedBox(height: 12),
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final p in const [['today', 'Today'], ['yday', 'Yesterday'], ['7d', '7 days'], ['30d', '30 days']])
          ChoiceChip(label: Text(p[1]), selected: period == p[0], onSelected: (_) {
            period = p[0];
            _load();
          }),
        ActionChip(
          avatar: const Icon(Icons.date_range, size: 18),
          label: Text(period == 'range' && range != null ? '${range!.start.day}/${range!.start.month}-${range!.end.day}/${range!.end.month}' : 'Range'),
          onPressed: () async {
            final now = DateTime.now();
            final r = await showDateRangePicker(context: context, firstDate: DateTime(now.year - 5), lastDate: now);
            if (r == null) return;
            range = r;
            period = 'range';
            _load();
          },
        ),
      ]),
      if (loading) const Padding(padding: EdgeInsets.only(top: 8), child: LinearProgressIndicator()),
      if (error != null) Text('Error: $error'),
      if (data != null) ...[
        const SizedBox(height: 10),
        Text('${totL.toStringAsFixed(0)} L in $totM min${daysIrr != null ? ' · irrigated on $daysIrr day${daysIrr == 1 ? '' : 's'}' : ''}',
            style: t.titleMedium),
        const SizedBox(height: 8),
        SizedBox(
          height: 180,
          child: rows.isEmpty
              ? const Center(child: Text('no records'))
              : BarChart(BarChartData(
                  barGroups: [
                    for (var i = 0; i < rows.length; i++)
                      BarChartGroupData(x: i, barRods: [
                        BarChartRodData(toY: rows[i].l, color: Theme.of(context).colorScheme.primary, width: rows.length > 40 ? 3 : 8),
                      ]),
                  ],
                  titlesData: FlTitlesData(
                    topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                    rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                    leftTitles: const AxisTitles(axisNameWidget: Text('litres'), sideTitles: SideTitles(showTitles: true, reservedSize: 40)),
                    bottomTitles: AxisTitles(
                      axisNameWidget: Text(data!['res'] == 'hour' ? 'hour of day' : 'day'),
                      sideTitles: SideTitles(
                        showTitles: rows.length <= 31,
                        reservedSize: 22,
                        interval: rows.length > 12 ? 5 : 1,
                        getTitlesWidget: (x, _) {
                          final i = x.toInt();
                          if (i < 0 || i >= rows.length) return const SizedBox();
                          final d = DateTime.fromMillisecondsSinceEpoch(rows[i].ts * 1000);
                          return Text(data!['res'] == 'hour' ? '${d.hour}' : '${d.day}', style: const TextStyle(fontSize: 10));
                        },
                      ),
                    ),
                  ),
                  gridData: const FlGridData(drawVerticalLine: false),
                  borderData: FlBorderData(show: false),
                )),
        ),
        const SizedBox(height: 8),
        if (data!['res'] == 'day')
          for (final r in rows.reversed.where((r) => r.m > 0))
            ListTile(
              dense: true,
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.water_drop, color: Colors.blue),
              title: Text(fmtWhen(r.ts).split(' ').take(2).join(' ')),
              trailing: Text('${r.m} min · ${r.l.toStringAsFixed(0)} L'),
            ),
      ],
      const SizedBox(height: 12),
      OutlinedButton.icon(
        icon: const Icon(Icons.open_in_new),
        label: const Text('Open node'),
        onPressed: () => Navigator.push(context,
            MaterialPageRoute(builder: (_) => NodeScreen(entry: widget.node, store: widget.store, shared: widget.ctl))),
      ),
    ]);
  }

  Widget _kv(String k, String v) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          SizedBox(width: 80, child: Text(k, style: Theme.of(context).textTheme.bodySmall)),
          Expanded(child: Text(v)),
        ]),
      );
}
