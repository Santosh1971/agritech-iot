import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:fl_chart/fl_chart.dart';
import '../models/history_entry.dart';
import '../providers/providers.dart';

enum _RangePreset { week, month, custom }

class HistoryGraphScreen extends ConsumerStatefulWidget {
  final List<HistoryEntry> initialEntries;
  final DateTime initialRangeStart;
  final DateTime initialRangeEnd;
  const HistoryGraphScreen({
    super.key,
    required this.initialEntries,
    required this.initialRangeStart,
    required this.initialRangeEnd,
  });

  @override
  ConsumerState<HistoryGraphScreen> createState() => _HistoryGraphScreenState();
}

class _HistoryGraphScreenState extends ConsumerState<HistoryGraphScreen> {
  late List<HistoryEntry> _entries;
  late DateTime _rangeStart;
  late DateTime _rangeEnd;
  _RangePreset _preset = _RangePreset.month;
  bool _loading = false;
  StreamSubscription<List<HistoryEntry>>? _sub;

  @override
  void initState() {
    super.initState();
    _entries    = widget.initialEntries;
    _rangeStart = widget.initialRangeStart;
    _rangeEnd   = widget.initialRangeEnd;
    _sub = ref.read(deviceServiceProvider).historyStream.listen((entries) {
      if (mounted) setState(() { _entries = entries; _loading = false; });
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  void _fetchRange(DateTime start, DateTime end) {
    setState(() { _loading = true; _rangeStart = start; _rangeEnd = end; });
    ref.read(deviceServiceProvider).getHistoryRange(start, end);
  }

  void _selectWeek() {
    setState(() => _preset = _RangePreset.week);
    final now = DateTime.now();
    _fetchRange(now.subtract(const Duration(days: 7)), now);
  }

  void _selectMonth() {
    setState(() => _preset = _RangePreset.month);
    final now = DateTime.now();
    _fetchRange(now.subtract(const Duration(days: 30)), now);
  }

  Future<void> _selectCustom() async {
    final now = DateTime.now();
    final picked = await showDateRangePicker(
      context: context,
      firstDate: DateTime(now.year - 1),
      lastDate: now,
      initialDateRange: DateTimeRange(start: _rangeStart, end: _rangeEnd),
    );
    if (picked != null) {
      setState(() => _preset = _RangePreset.custom);
      _fetchRange(picked.start, picked.end);
    }
  }

  List<double> _dailyTotals() {
    final startDay = DateTime(_rangeStart.year, _rangeStart.month, _rangeStart.day);
    final days = _rangeEnd.difference(startDay).inDays + 1;
    final totals = List<double>.filled(days, 0);
    for (final e in _entries) {
      final dayIndex = e.dateTime.difference(startDay).inDays;
      if (dayIndex >= 0 && dayIndex < days) {
        totals[dayIndex] += e.litersDelivered;
      }
    }
    return totals;
  }

  @override
  Widget build(BuildContext context) {
    final totals = _dailyTotals();
    final hasData = totals.any((t) => t > 0);
    final average = totals.isEmpty
        ? 0.0
        : totals.reduce((a, b) => a + b) / totals.length;
    final peak = totals.isEmpty
        ? 0.0
        : totals.reduce((a, b) => a > b ? a : b);
    final maxY = (peak <= 0 ? 1.0 : peak) * 1.25;
    final startDay = DateTime(_rangeStart.year, _rangeStart.month, _rangeStart.day);
    // fl_chart's SideTitles.interval isn't reliably honored for BarChart in
    // this version, so we filter which labels render ourselves instead.
    final labelStep = totals.length > 10 ? 7 : 1;

    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        leading: BackButton(color: Theme.of(context).colorScheme.onSurface),
        title: Text('Water Usage Trend',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface, fontWeight: FontWeight.w600)),
        centerTitle: true,
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(children: [
              _PresetChip(label: 'Week', selected: _preset == _RangePreset.week,
                  onTap: _selectWeek),
              const SizedBox(width: 8),
              _PresetChip(label: 'Month', selected: _preset == _RangePreset.month,
                  onTap: _selectMonth),
              const SizedBox(width: 8),
              _PresetChip(label: 'Custom', selected: _preset == _RangePreset.custom,
                  onTap: _selectCustom),
            ]),
            const SizedBox(height: 16),
            Text('${_fmtDate(startDay)} – ${_fmtDate(_rangeEnd)}',
                style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 13)),
            const SizedBox(height: 4),
            Row(children: [
              Text('Avg ${average.toStringAsFixed(1)} L/day',
                  style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 18,
                      color: Color(0xFF2196F3))),
              const SizedBox(width: 16),
              Text('Peak ${peak.toStringAsFixed(1)} L',
                  style: TextStyle(fontWeight: FontWeight.w600, fontSize: 14,
                      color: Colors.grey.shade600)),
            ]),
            const SizedBox(height: 24),
            Expanded(
              child: _loading
                  ? const Center(child: CircularProgressIndicator(
                      color: Color(0xFF2196F3)))
                  : !hasData
                      ? Center(child: Text('No data in this period',
                          style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant)))
                      : BarChart(
                          BarChartData(
                            minY: 0,
                            maxY: maxY,
                            gridData: const FlGridData(show: true, drawVerticalLine: false),
                            titlesData: FlTitlesData(
                              leftTitles: const AxisTitles(
                                sideTitles: SideTitles(showTitles: true, reservedSize: 36),
                              ),
                              bottomTitles: AxisTitles(
                                sideTitles: SideTitles(
                                  showTitles: true,
                                  reservedSize: 28,
                                  getTitlesWidget: (value, meta) {
                                    final idx = value.toInt();
                                    if (idx < 0 || idx >= totals.length) {
                                      return const SizedBox();
                                    }
                                    // Only render labels on step boundaries
                                    // (or the final day), skip the rest.
                                    final isLast = idx == totals.length - 1;
                                    if (idx % labelStep != 0 && !isLast) {
                                      return const SizedBox();
                                    }
                                    final date = startDay.add(Duration(days: idx));
                                    return Padding(
                                      padding: const EdgeInsets.only(top: 6),
                                      child: Text('${date.day}/${date.month}',
                                          style: const TextStyle(
                                              fontSize: 10, color: Colors.grey)),
                                    );
                                  },
                                ),
                              ),
                              topTitles: const AxisTitles(
                                  sideTitles: SideTitles(showTitles: false)),
                              rightTitles: const AxisTitles(
                                  sideTitles: SideTitles(showTitles: false)),
                            ),
                            borderData: FlBorderData(show: false),
                            barGroups: [
                              for (int i = 0; i < totals.length; i++)
                                BarChartGroupData(x: i, barRods: [
                                  BarChartRodData(
                                    toY: totals[i],
                                    color: const Color(0xFF2196F3),
                                    width: (300 / totals.length).clamp(3, 14),
                                    borderRadius: BorderRadius.circular(2),
                                  ),
                                ]),
                            ],
                            extraLinesData: ExtraLinesData(horizontalLines: [
                              HorizontalLine(
                                y: average,
                                color: Colors.orange,
                                strokeWidth: 2,
                                dashArray: const [6, 4],
                                label: HorizontalLineLabel(
                                  show: true,
                                  alignment: Alignment.topRight,
                                  style: const TextStyle(color: Colors.orange,
                                      fontSize: 11, fontWeight: FontWeight.w600),
                                  labelResolver: (line) =>
                                      'Avg ${average.toStringAsFixed(1)}L',
                                ),
                              ),
                            ]),
                          ),
                        ),
            ),
          ],
        ),
      ),
    );
  }

  String _fmtDate(DateTime d) =>
      '${d.day.toString().padLeft(2, '0')}/${d.month.toString().padLeft(2, '0')}/${d.year}';
}

class _PresetChip extends StatelessWidget {
  final String label;
  final bool selected;
  final VoidCallback onTap;
  const _PresetChip({required this.label, required this.selected, required this.onTap});
  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      decoration: BoxDecoration(
        color: selected ? const Color(0xFF2196F3) : Colors.grey.shade100,
        borderRadius: BorderRadius.circular(20)),
      child: Text(label,
          style: TextStyle(
              color: selected ? Colors.white : Colors.grey,
              fontWeight: FontWeight.w500)),
    ),
  );
}
