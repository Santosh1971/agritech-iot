import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../services/node_store.dart';

class HistoryTab extends StatefulWidget {
  const HistoryTab({super.key, required this.ctl});
  final NodeController ctl;
  @override
  State<HistoryTab> createState() => _HistoryTabState();
}

class _HistoryTabState extends State<HistoryTab> {
  String period = 'today';
  Map<String, dynamic>? data;
  List<Map<String, dynamic>> days = [];
  bool loading = false;
  String? error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  int _sec(DateTime d) => d.millisecondsSinceEpoch ~/ 1000;

  Future<void> _load({DateTimeRange? range}) async {
    setState(() {
      loading = true;
      error = null;
    });
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    late DateTime from, to;
    String res = '';
    switch (period) {
      case 'today':
        from = today;
        to = now;
        break;
      case 'yday':
        from = today.subtract(const Duration(days: 1));
        to = today.subtract(const Duration(seconds: 1));
        break;
      case '7d':
        from = today.subtract(const Duration(days: 6));
        to = now;
        res = 'hour';
        break;
      case '30d':
        from = today.subtract(const Duration(days: 29));
        to = now;
        res = 'day';
        break;
      case '1y':
        from = today.subtract(const Duration(days: 364));
        to = now;
        res = 'day';
        break;
      case 'range':
        from = range!.start;
        to = range.end.add(const Duration(hours: 23, minutes: 59, seconds: 59));
        break;
    }
    final r = await widget.ctl.send({'cmd': 'get_history', 'from': _sec(from), 'to': _sec(to), 'res': res},
        timeout: const Duration(seconds: 30));
    final nDays = to.difference(from).inDays + 2;
    final d = await widget.ctl.send({'cmd': 'get_days', 'days': nDays});
    if (!mounted) return;
    setState(() {
      loading = false;
      if (r['ok'] == true) {
        data = Map<String, dynamic>.from(r['data']);
      } else {
        error = r['err']?.toString();
      }
      days = ((d['data']?['days'] as List?) ?? []).map((e) => Map<String, dynamic>.from(e)).toList().reversed.toList();
    });
  }

  @override
  Widget build(BuildContext context) {
    final t = Theme.of(context).textTheme;
    final tot = data == null ? null : Map<String, dynamic>.from(data!['totals'] ?? {});
    return ListView(padding: const EdgeInsets.all(12), children: [
      Wrap(spacing: 6, runSpacing: 6, children: [
        for (final p in const [
          ['today', 'Today'],
          ['yday', 'Yesterday'],
          ['7d', 'Last 7 days'],
          ['30d', 'Last 30 days'],
          ['1y', 'Last year'],
        ])
          ChoiceChip(
            label: Text(p[1]),
            selected: period == p[0],
            onSelected: (_) {
              period = p[0];
              _load();
            },
          ),
        ActionChip(
          avatar: const Icon(Icons.date_range, size: 18),
          label: const Text('Date range'),
          onPressed: () async {
            final now = DateTime.now();
            final r = await showDateRangePicker(
                context: context, firstDate: DateTime(now.year - 5), lastDate: now, initialEntryMode: DatePickerEntryMode.input);
            if (r == null) return;
            period = 'range';
            _load(range: r);
          },
        ),
      ]),
      const SizedBox(height: 8),
      if (loading) const LinearProgressIndicator(),
      if (error != null) Text('Error: $error', style: TextStyle(color: Theme.of(context).colorScheme.error)),
      if (tot != null)
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Totals', style: t.titleMedium),
              const SizedBox(height: 4),
              Text('Valve 1: ${tot['valve_litres']?[0]} L, open ${tot['valve_open_min']?[0]} min'),
              Text('Valve 2: ${tot['valve_litres']?[1]} L, open ${tot['valve_open_min']?[1]} min'),
              Text('Water in pipe ${tot['water_min']} min  |  lowest battery ${tot['min_batt']} V'),
              Text('Per sensor: ${(tot['litres'] as List?)?.join(' / ')} L', style: t.bodySmall),
            ]),
          ),
        ),
      if (data != null) Card(child: Padding(padding: const EdgeInsets.fromLTRB(8, 16, 16, 8), child: _chart(context))),
      Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text('Day results', style: t.titleMedium),
            if (days.isEmpty) const Text('none in this period'),
            for (final d in days)
              ListTile(
                dense: true,
                contentPadding: EdgeInsets.zero,
                leading: Icon(
                  d['status'] == 'done' ? Icons.check_circle : d['status'] == 'partial' ? Icons.timelapse : Icons.water_drop_outlined,
                  color: d['status'] == 'done' ? Colors.green : Colors.orange,
                ),
                title: Text('${d['date']}  V${d['valve']}  ${d['status']}'),
                subtitle: Text('${d['run_min']} of ${d['planned_min'] ?? d['planned_l']} ${d['planned_l'] != null ? 'L' : 'min'}'
                    '  |  ${(d['litres'] as List?)?.join(' + ')} L (normal ${(d['normal_litres'] as List?)?.join(' + ')})'),
              ),
          ]),
        ),
      ),
    ]);
  }

  Widget _chart(BuildContext context) {
    final pts = (data!['points'] as List? ?? []).cast<List>();
    if (pts.isEmpty) return const SizedBox(height: 60, child: Center(child: Text('no records in this period')));
    final raw = data!['res'] == 'raw';
    final cs = Theme.of(context).colorScheme;
    double y(List p, int a, int b) => ((p[a] as num) + (p[b] as num)).toDouble();
    final t0 = (pts.first[0] as num).toDouble();
    if (raw) {
      // L/min per valve over time.
      LineChartBarData line(int a, int b, Color c) => LineChartBarData(
            spots: [for (final p in pts) FlSpot(((p[0] as num) - t0) / 3600, y(p, a, b))],
            color: c,
            dotData: const FlDotData(show: false),
            barWidth: 2,
          );
      return SizedBox(
        height: 200,
        child: LineChart(LineChartData(
          lineBarsData: [line(1, 2, cs.primary), line(3, 4, Colors.orange)],
          titlesData: _titles('h', 'L/min'),
          gridData: const FlGridData(drawVerticalLine: false),
          borderData: FlBorderData(show: false),
        )),
      );
    }
    // Litres per bucket, V1 and V2 side by side.
    return SizedBox(
      height: 200,
      child: BarChart(BarChartData(
        barGroups: [
          for (var i = 0; i < pts.length; i++)
            BarChartGroupData(x: i, barRods: [
              BarChartRodData(toY: y(pts[i], 1, 2), color: cs.primary, width: pts.length > 60 ? 2 : 5),
              BarChartRodData(toY: y(pts[i], 3, 4), color: Colors.orange, width: pts.length > 60 ? 2 : 5),
            ]),
        ],
        titlesData: _titles(data!['res'] == 'hour' ? 'hour' : 'day', 'litres'),
        gridData: const FlGridData(drawVerticalLine: false),
        borderData: FlBorderData(show: false),
      )),
    );
  }

  FlTitlesData _titles(String xUnit, String yUnit) => FlTitlesData(
        topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        bottomTitles: AxisTitles(axisNameWidget: Text('$xUnit  (green V1, orange V2)'), sideTitles: const SideTitles(showTitles: false)),
        leftTitles: AxisTitles(axisNameWidget: Text(yUnit), sideTitles: const SideTitles(showTitles: true, reservedSize: 40)),
      );
}
