import 'dart:async';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../models/device_status.dart';
import '../models/history_record.dart';
import '../services/device_service.dart';

enum _Metric { time, volume }

/// Run history — date-grouped, up to whatever the device has retained
/// (RunHistory.h caps this at roughly 3 months of realistic use), time
/// and volume (where a flow meter is present) with a daily average,
/// covering both scheduled and manually-toggled runs.
///
/// Works over either transport: Local reads its reply off responseStream,
/// Cloud gets it via a dedicated MQTT topic (no periodic broadcast exists
/// for history the way there is for status/programs) — see historyStream
/// on DeviceService. Cloud requests a smaller page (see _maxForMode)
/// since the broker path has a real payload-size ceiling that Local's
/// WebSocket doesn't.
class HistoryScreen extends ConsumerStatefulWidget {
  const HistoryScreen({super.key});
  @override
  ConsumerState<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends ConsumerState<HistoryScreen> {
  StreamSubscription<List<HistoryRecord>>? _sub;
  DeviceService? _subscribedTo;
  List<HistoryRecord> _records = [];
  bool _loading = true;
  _Metric _metric = _Metric.time;
  final Set<DateTime> _expanded = {};

  // Resubscribes when the active transport changes (e.g. the user
  // switches Local <-> Cloud while this screen is open) and, since
  // that means whatever we last fetched came from a now-inactive
  // service, kicks off a fresh fetch on the new one rather than
  // leaving stale results on screen.
  void _ensureSubscribed(DeviceService service, TransportMode mode) {
    if (identical(_subscribedTo, service)) return;
    _sub?.cancel();
    _subscribedTo = service;
    _sub = service.historyStream.listen(_onHistory);
    WidgetsBinding.instance.addPostFrameCallback((_) => _fetch(service, mode));
  }

  void _fetch(DeviceService service, TransportMode mode) {
    setState(() => _loading = true);
    // 3 months back, generously — the device only ever has at most what
    // it actually retained, so asking for more than that is harmless.
    final since = DateTime.now().subtract(const Duration(days: 92));
    final sinceEpoch = DateTime.utc(since.year, since.month, since.day).millisecondsSinceEpoch ~/ 1000;
    // Local's WebSocket has no real size ceiling; Cloud's MQTT buffer
    // does (see MqttClientWrapper.h) — matches FG1's own precedent of
    // ~150-200 history entries fitting its buffer comfortably.
    final max = mode == TransportMode.local ? 2000 : 150;
    service.getHistory(since: sinceEpoch, max: max);
  }

  void _onHistory(List<HistoryRecord> list) {
    if (mounted) setState(() { _records = list; _loading = false; });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final service = ref.watch(deviceServiceProvider);
    final mode = ref.watch(transportModeProvider);
    _ensureSubscribed(service, mode);

    final names = ref.watch(deviceStatusProvider).valueOrNull?.relayNames ?? const RelayNames();
    final days = DailyHistory.groupByDay(_records);

    return Scaffold(
      appBar: AppBar(
        title: const Text('Run History', style: TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [IconButton(onPressed: () => _fetch(service, mode), icon: const Icon(Icons.refresh))],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : days.isEmpty
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Text(
                      mode == TransportMode.local
                          ? 'No history yet. Make sure you\'re connected to the device\'s WiFi and try refreshing.'
                          : 'No history yet. Make sure the device is online in Cloud mode and try refreshing.',
                      textAlign: TextAlign.center,
                      style: TextStyle(color: Colors.grey.shade600),
                    ),
                  ),
                )
              : ListView(
                  padding: const EdgeInsets.all(16),
                  children: [
                    _SummaryCard(days: days, metric: _metric, onMetricChanged: (m) => setState(() => _metric = m)),
                    const SizedBox(height: 16),
                    _ChartCard(days: days, metric: _metric),
                    const SizedBox(height: 16),
                    const Text('BY DAY', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
                    const SizedBox(height: 8),
                    for (final day in days.reversed)
                      _DayTile(
                        day: day,
                        names: names,
                        expanded: _expanded.contains(day.day),
                        onToggle: () => setState(() {
                          _expanded.contains(day.day) ? _expanded.remove(day.day) : _expanded.add(day.day);
                        }),
                      ),
                  ],
                ),
    );
  }
}

class _SummaryCard extends StatelessWidget {
  final List<DailyHistory> days;
  final _Metric metric;
  final ValueChanged<_Metric> onMetricChanged;
  const _SummaryCard({required this.days, required this.metric, required this.onMetricChanged});

  @override
  Widget build(BuildContext context) {
    final totalMinutes = days.fold(0, (sum, d) => sum + d.totalDurationSec) / 60;
    final totalLiters = days.fold(0.0, (sum, d) => sum + d.totalVolumeLiters);
    final avgMinutesPerDay = days.isEmpty ? 0.0 : totalMinutes / days.length;
    final avgLitersPerDay = days.isEmpty ? 0.0 : totalLiters / days.length;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Expanded(child: _statBlock('${totalMinutes.toStringAsFixed(0)} min', 'Total time', '${days.length} day(s)')),
          Expanded(child: _statBlock('${totalLiters.toStringAsFixed(0)} L', 'Total volume', '')),
          Expanded(child: _statBlock('${avgMinutesPerDay.toStringAsFixed(0)} min', 'Avg / day', '${avgLitersPerDay.toStringAsFixed(1)} L avg')),
        ]),
        const SizedBox(height: 12),
        SegmentedButton<_Metric>(
          segments: const [
            ButtonSegment(value: _Metric.time, label: Text('Time'), icon: Icon(Icons.timer_outlined, size: 16)),
            ButtonSegment(value: _Metric.volume, label: Text('Volume'), icon: Icon(Icons.water_drop_outlined, size: 16)),
          ],
          selected: {metric},
          onSelectionChanged: (s) => onMetricChanged(s.first),
          style: const ButtonStyle(visualDensity: VisualDensity.compact),
        ),
      ]),
    );
  }

  Widget _statBlock(String value, String label, String sub) => Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(value, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 16)),
        Text(label, style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
        if (sub.isNotEmpty) Text(sub, style: TextStyle(fontSize: 10, color: Colors.grey.shade500)),
      ]);
}

class _ChartCard extends StatelessWidget {
  final List<DailyHistory> days;
  final _Metric metric;
  const _ChartCard({required this.days, required this.metric});

  @override
  Widget build(BuildContext context) {
    final values = days
        .map((d) => metric == _Metric.time ? d.totalDurationSec / 60.0 : d.totalVolumeLiters)
        .toList();
    const barWidth = 12.0;
    final chartWidth = max(MediaQuery.of(context).size.width - 64, values.length * barWidth);

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(metric == _Metric.time ? 'MINUTES PER DAY' : 'LITERS PER DAY',
            style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
        const SizedBox(height: 8),
        SingleChildScrollView(
          scrollDirection: Axis.horizontal,
          reverse: true, // most recent day visible by default
          child: SizedBox(
            width: chartWidth,
            height: 100,
            child: CustomPaint(painter: _BarChartPainter(values, const Color(0xFF2196F3))),
          ),
        ),
        if (days.isNotEmpty) ...[
          const SizedBox(height: 4),
          Row(mainAxisAlignment: MainAxisAlignment.spaceBetween, children: [
            Text(_fmtDate(days.first.day), style: TextStyle(fontSize: 10, color: Colors.grey.shade500)),
            Text(_fmtDate(days.last.day), style: TextStyle(fontSize: 10, color: Colors.grey.shade500)),
          ]),
        ],
      ]),
    );
  }

  String _fmtDate(DateTime d) => '${d.day}/${d.month}';
}

class _BarChartPainter extends CustomPainter {
  final List<double> values;
  final Color color;
  _BarChartPainter(this.values, this.color);

  @override
  void paint(Canvas canvas, Size size) {
    if (values.isEmpty) return;
    final maxVal = values.reduce(max);
    final safeMax = maxVal <= 0 ? 1.0 : maxVal;
    final barWidth = size.width / values.length;
    final paint = Paint()..color = color;

    for (int i = 0; i < values.length; i++) {
      final h = (values[i] / safeMax) * (size.height - 4);
      final rect = Rect.fromLTWH(i * barWidth + 1, size.height - h, (barWidth - 2).clamp(1, barWidth), h);
      canvas.drawRRect(
        RRect.fromRectAndCorners(rect, topLeft: const Radius.circular(2), topRight: const Radius.circular(2)),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(covariant _BarChartPainter oldDelegate) =>
      oldDelegate.values.length != values.length || oldDelegate.color != color;
}

class _DayTile extends StatelessWidget {
  final DailyHistory day;
  final RelayNames names;
  final bool expanded;
  final VoidCallback onToggle;
  const _DayTile({required this.day, required this.names, required this.expanded, required this.onToggle});

  @override
  Widget build(BuildContext context) {
    final minutes = day.totalDurationSec / 60;
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(10),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.04), blurRadius: 6, offset: const Offset(0, 1))],
      ),
      child: Column(children: [
        ListTile(
          onTap: onToggle,
          title: Text(_fmtDate(day.day), style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
          subtitle: Text('${minutes.toStringAsFixed(0)} min • ${day.totalVolumeLiters.toStringAsFixed(1)} L • ${day.records.length} run(s)',
              style: TextStyle(fontSize: 11, color: Colors.grey.shade600)),
          trailing: Icon(expanded ? Icons.expand_less : Icons.expand_more),
        ),
        if (expanded)
          Padding(
            padding: const EdgeInsets.only(left: 16, right: 16, bottom: 8),
            child: Column(children: [
              for (final r in day.records.reversed) _RecordRow(record: r, names: names),
            ]),
          ),
      ]),
    );
  }

  String _fmtDate(DateTime d) {
    const wd = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
    return '${wd[d.weekday - 1]}, ${d.day}/${d.month}/${d.year}';
  }
}

class _RecordRow extends StatelessWidget {
  final HistoryRecord record;
  final RelayNames names;
  const _RecordRow({required this.record, required this.names});

  @override
  Widget build(BuildContext context) {
    final mins = record.durationSec / 60;
    final time = '${record.start.hour.toString().padLeft(2, '0')}:${record.start.minute.toString().padLeft(2, '0')}';
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(children: [
        Icon(record.isManual ? Icons.pan_tool_outlined : Icons.schedule, size: 14, color: Colors.grey.shade500),
        const SizedBox(width: 6),
        Text(time, style: TextStyle(fontSize: 11, color: Colors.grey.shade500)),
        const SizedBox(width: 8),
        Expanded(child: Text(record.displayName(names), style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w500))),
        Text('${mins.toStringAsFixed(0)} min', style: const TextStyle(fontSize: 11)),
        if (record.volumeLiters > 0) ...[
          const SizedBox(width: 6),
          Text('${record.volumeLiters.toStringAsFixed(1)} L', style: TextStyle(fontSize: 11, color: Colors.blue.shade700)),
        ],
      ]),
    );
  }
}
