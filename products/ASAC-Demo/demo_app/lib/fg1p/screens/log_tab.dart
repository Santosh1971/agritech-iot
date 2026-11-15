import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';

/// Last 24 h of control/outside T and RH (5-min points, kept in the
/// controller's memory) with misting periods shaded, plus the event log.
class LogTab extends StatefulWidget {
  const LogTab({super.key, required this.conn});
  final HouseConn conn;
  @override
  State<LogTab> createState() => _LogTabState();
}

class _LogTabState extends State<LogTab> {
  List<List<num?>> _trend = [];
  List<Map<String, dynamic>> _events = [];
  bool _loading = false;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() => _loading = true);
    final t = await widget.conn.send({'cmd': 'get_trend'});
    final e = await widget.conn.send({'cmd': 'get_events'});
    if (!mounted) return;
    setState(() {
      _loading = false;
      if (t['data'] is List) {
        _trend = [for (final r in t['data'] as List) [for (final v in r as List) v as num?]];
      }
      if (e['data'] is List) {
        _events = [for (final x in e['data'] as List) Map<String, dynamic>.from(x)].reversed.toList();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final set = St(widget.conn.status!).settings;
    final cs = Theme.of(context).colorScheme;
    return RefreshIndicator(
      onRefresh: _load,
      child: ListView(padding: const EdgeInsets.only(bottom: 32), children: [
        SectionTitle('Last 24 hours',
            trailing: _loading
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                : IconButton(onPressed: _load, icon: const Icon(Icons.refresh), visualDensity: VisualDensity.compact)),
        if (_trend.length < 2)
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text('Not enough data yet — a point is added every 5 minutes. '
                'History restarts if the controller loses power.'),
          )
        else ...[
          _chart(context, 'Temperature °C', 1, 3, [(set['t_start'] as num?)?.toDouble(), (set['t_stop'] as num?)?.toDouble()]),
          _chart(context, 'Humidity %', 2, 4, [(set['rh_low'] as num?)?.toDouble(), (set['rh_high'] as num?)?.toDouble()]),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Wrap(spacing: 16, children: [
              _legend(cs.primary, 'Inside (control)'),
              _legend(Colors.orange, 'Outside'),
              _legend(const Color(0xFF1E88E5).withValues(alpha: 0.25), 'Misting'),
            ]),
          ),
        ],
        const SectionTitle('Events'),
        if (_events.isEmpty) const Padding(padding: EdgeInsets.all(16), child: Text('No events yet')),
        for (final e in _events)
          ListTile(
            dense: true,
            leading: Icon(_icon(e['msg'] ?? ''), size: 20),
            title: Text(e['msg'] ?? ''),
            subtitle: Text(_when(e)),
          ),
      ]),
    );
  }

  String _when(Map<String, dynamic> e) {
    final ts = (e['ts'] as num?)?.toInt() ?? 0;
    if (ts > 0) {
      final d = DateTime.fromMillisecondsSinceEpoch(ts * 1000, isUtc: true);
      return '${d.day}/${d.month}  ${hhmmMin(d.hour * 60 + d.minute)}:${d.second.toString().padLeft(2, '0')}';
    }
    return 'uptime ${mmss((e['up'] as num?)?.toInt() ?? 0)} (clock not set)';
  }

  IconData _icon(String m) {
    if (m.startsWith('Pump ON')) return Icons.water_drop;
    if (m.startsWith('Pump OFF')) return Icons.water_drop_outlined;
    if (m.startsWith('Sensor')) return Icons.sensors;
    if (m.startsWith('Settings') || m.startsWith('Control')) return Icons.tune;
    if (m.contains('WiFi')) return Icons.wifi;
    if (m.startsWith('Power')) return Icons.power;
    return Icons.info_outline;
  }

  Widget _legend(Color c, String t) => Row(mainAxisSize: MainAxisSize.min, children: [
        Container(width: 14, height: 4, color: c),
        const SizedBox(width: 6),
        Text(t, style: const TextStyle(fontSize: 12)),
      ]);

  Widget _chart(BuildContext context, String title, int iIn, int iOut, List<double?> limits) {
    final cs = Theme.of(context).colorScheme;
    final t0 = _trend.first[0]!.toDouble();
    double x(List<num?> r) => (r[0]!.toDouble() - t0) / 3600.0;
    List<FlSpot> spots(int i) => [
          for (final r in _trend) r[i] == null ? FlSpot.nullSpot : FlSpot(x(r), r[i]! / 10.0),
        ];
    final misting = <VerticalRangeAnnotation>[];
    for (final r in _trend) {
      if ((r[5] ?? 0) > 0) {
        misting.add(VerticalRangeAnnotation(
            x1: x(r) - 5 / 60, x2: x(r), color: const Color(0xFF1E88E5).withValues(alpha: 0.18)));
      }
    }
    String clock(double h) {
      final ts = t0 + h * 3600;
      final m = ((ts % 86400) / 60).floor();
      return hhmmMin(m);
    }

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(4, 12, 16, 8),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Padding(padding: const EdgeInsets.only(left: 12, bottom: 8), child: Text(title)),
          SizedBox(
            height: 180,
            child: LineChart(LineChartData(
              rangeAnnotations: RangeAnnotations(verticalRangeAnnotations: misting),
              extraLinesData: ExtraLinesData(horizontalLines: [
                for (final l in limits)
                  if (l != null)
                    HorizontalLine(y: l, color: cs.error.withValues(alpha: 0.5), strokeWidth: 1, dashArray: [4, 4]),
              ]),
              gridData: const FlGridData(drawVerticalLine: false),
              borderData: FlBorderData(show: false),
              titlesData: FlTitlesData(
                topTitles: const AxisTitles(),
                rightTitles: const AxisTitles(),
                leftTitles: AxisTitles(
                    sideTitles: SideTitles(
                        showTitles: true,
                        reservedSize: 34,
                        interval: iIn == 1 ? 5 : 10,
                        getTitlesWidget: (v, m) => v == m.min || v == m.max
                            ? const SizedBox()
                            : Text(v.toStringAsFixed(0), style: const TextStyle(fontSize: 10)))),
                bottomTitles: AxisTitles(
                    sideTitles: SideTitles(
                        showTitles: true,
                        interval: 6,
                        reservedSize: 22,
                        getTitlesWidget: (v, m) => Text(clock(v), style: const TextStyle(fontSize: 10)))),
              ),
              lineTouchData: const LineTouchData(enabled: true),
              lineBarsData: [
                LineChartBarData(spots: spots(iOut), color: Colors.orange, barWidth: 1.5, dotData: const FlDotData(show: false)),
                LineChartBarData(spots: spots(iIn), color: cs.primary, barWidth: 2.5, dotData: const FlDotData(show: false)),
              ],
            )),
          ),
        ]),
      ),
    );
  }
}
