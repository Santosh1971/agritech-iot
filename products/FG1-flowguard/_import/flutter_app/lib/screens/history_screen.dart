import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:intl/intl.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/providers.dart';
import '../models/history_entry.dart';
import '../models/cycle.dart';
import 'history_graph_screen.dart';

const String _kHistoryCacheKey = 'cached_history';

class HistoryScreen extends ConsumerStatefulWidget {
  const HistoryScreen({super.key});
  @override
  ConsumerState<HistoryScreen> createState() => _HistoryScreenState();
}

class _HistoryScreenState extends ConsumerState<HistoryScreen> {
  String _filter = 'All';
  DateTime _date = DateTime.now();
  List<HistoryEntry> _entries = [];
  bool _loading = true;
  bool _didInitialRequest = false;

  @override
  void initState() {
    super.initState();
    _loadFromCache();
    // Loading timeout — fall back to whatever we have (cache or empty)
    // if nothing arrives within 3s. The actual data subscription now
    // lives in build() via ref.listen(historyProvider, ...) — see there
    // for why.
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted && _loading) setState(() => _loading = false);
    });
  }

  DateTime? _lastRequestAt;

  void _requestHistory() {
    // Debounced — the actual cause of the out-of-order-response race
    // was firing a fresh request on every single reconnect, and with
    // reconnects sometimes happening in quick succession (WiFi
    // flapping), more than one request could be in flight before the
    // first response ever came back. Skipping a request that's within
    // 5s of the last one makes that overlap very unlikely without
    // needing to guess at which response is "newer" on the receiving
    // end.
    final now = DateTime.now();
    if (_lastRequestAt != null && now.difference(_lastRequestAt!) < const Duration(seconds: 5)) {
      return;
    }
    _lastRequestAt = now;
    ref.read(deviceServiceProvider)
        .getHistoryRange(now.subtract(const Duration(days: 30)), now);
  }

  Future<void> _loadFromCache() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_kHistoryCacheKey);
    if (raw == null || !mounted) return;
    try {
      final list = (jsonDecode(raw) as List)
          .map((e) => HistoryEntry.fromJson(e as Map<String, dynamic>))
          .toList();
      if (mounted && _entries.isEmpty) {
        setState(() { _entries = list; _loading = false; });
      }
    } catch (_) {
      // Corrupt cache — ignore, MQTT will repopulate
    }
  }

  Future<void> _saveToCache(List<HistoryEntry> entries) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = jsonEncode(entries.map((e) => e.toJson()).toList());
    await prefs.setString(_kHistoryCacheKey, raw);
  }

  @override
  Widget build(BuildContext context) {
    final filtered = _filterEntries(_entries);

    // Re-request history the moment the connection is (re)established —
    // same fix already applied to Dashboard/Cycles for this exact bug:
    // a one-shot initState() request can fire before the connection is
    // actually ready (screens are all built upfront via IndexedStack),
    // and previously never got retried — silently leaving History empty
    // regardless of which transport was in use, since the bug was purely
    // about request timing, not the transport itself.
    final connected = ref.watch(deviceConnectedProvider);

    // THE actual fix for "History stuck in MQTT mode, catches up after
    // switching to SoftAP": mqttServiceProvider rebuilds (a fresh
    // MqttService instance) whenever deviceSuffixProvider changes — that's
    // by design, for the per-device pairing feature. The old
    // initState()-based `ref.read(deviceServiceProvider).historyStream
    // .listen(...)` took a ONE-TIME snapshot of whichever instance
    // existed at that moment and kept listening to it forever, even
    // after it was replaced — so it silently stopped receiving anything
    // new. Dashboard never had this problem because it uses
    // ref.watch(historyProvider), which properly follows the current
    // instance at all times. ref.listen() here does the same thing, just
    // as a side effect (updating local state + cache) instead of a
    // direct widget rebuild.
    ref.listen(historyProvider, (prev, next) {
      next.whenData((entries) {
        if (mounted) setState(() { _entries = entries; _loading = false; });
        _saveToCache(entries);
      });
    });
    ref.listen(deviceConnectedProvider, (prev, next) {
      final wasConnected = prev ?? false;
      if (next && !wasConnected) _requestHistory();
    });
    if (!_didInitialRequest && connected) {
      _didInitialRequest = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _requestHistory());
    }

    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        leading: BackButton(color: Theme.of(context).colorScheme.onSurface),
        title: Text('History',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface, fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          IconButton(
            icon: Icon(Icons.show_chart, color: Theme.of(context).colorScheme.onSurface),
            tooltip: 'Usage trend',
            onPressed: () {
              final now = DateTime.now();
              Navigator.push(context, MaterialPageRoute(
                builder: (_) => HistoryGraphScreen(
                  initialEntries: _entries,
                  initialRangeStart: now.subtract(const Duration(days: 30)),
                  initialRangeEnd: now,
                ),
              ));
            },
          ),
          IconButton(
            icon: Icon(Icons.calendar_today, color: Theme.of(context).colorScheme.onSurface),
            onPressed: () async {
              final picked = await showDatePicker(
                context: context,
                initialDate: _date,
                firstDate: DateTime(2024),
                lastDate: DateTime.now(),
              );
              if (picked != null) setState(() => _date = picked);
            },
          ),
        ],
      ),
      body: Column(children: [
        Container(
          color: Theme.of(context).cardColor,
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
          child: Row(children: [
            _FilterTab('All',    selected: _filter == 'All',
                onTap: () => setState(() => _filter = 'All')),
            const SizedBox(width: 8),
            _FilterTab('Auto',   selected: _filter == 'Auto',
                onTap: () => setState(() => _filter = 'Auto')),
            const SizedBox(width: 8),
            _FilterTab('Manual', selected: _filter == 'Manual',
                onTap: () => setState(() => _filter = 'Manual')),
          ]),
        ),
        Container(
          color: Theme.of(context).cardColor,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Row(children: [
            IconButton(
              icon: const Icon(Icons.chevron_left),
              onPressed: () => setState(() =>
                  _date = _date.subtract(const Duration(days: 1))),
            ),
            Expanded(
              child: Center(
                child: Text(DateFormat('d MMM yyyy').format(_date),
                    style: const TextStyle(fontWeight: FontWeight.w600,
                        fontSize: 15)),
              ),
            ),
            IconButton(
              icon: const Icon(Icons.chevron_right),
              onPressed: _date.isBefore(DateTime.now().subtract(
                  const Duration(days: 1)))
                  ? () => setState(
                        () => _date = _date.add(const Duration(days: 1)))
                  : null,
            ),
          ]),
        ),
        const Divider(height: 1),
        Expanded(
          child: _loading
              ? const Center(child: CircularProgressIndicator(
                  color: Color(0xFF2196F3)))
              : filtered.isEmpty
                  ? _buildEmpty()
                  : _buildList(filtered),
        ),
        if (!_loading && filtered.isNotEmpty)
          Builder(builder: (_) {
            final totalLiters = filtered.fold<double>(
                0, (sum, e) => sum + e.litersDelivered);
            // Was always counting non-manual entries regardless of which
            // filter tab was active — on the Manual tab, `filtered`
            // already contains ONLY manual entries, so re-filtering for
            // "status != manual" within that always produced ~zero. This
            // should just be how many entries are actually shown.
            final totalCycles = filtered.length;
            return Container(
              color: Theme.of(context).cardColor,
              padding: const EdgeInsets.all(16),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceAround,
                children: [
                  _TotalItem(label: 'Total Water',
                      value: '${totalLiters.toStringAsFixed(1)} L',
                      color: const Color(0xFF2196F3)),
                  _TotalItem(label: 'Total Cycles',
                      value: '$totalCycles',
                      color: const Color(0xFF4CAF50)),
                ],
              ),
            );
          }),
      ]),
    );
  }

  List<HistoryEntry> _filterEntries(List<HistoryEntry> entries) {
    return entries.where((e) {
      final sameDay = e.dateTime.day == _date.day &&
          e.dateTime.month == _date.month &&
          e.dateTime.year == _date.year;
      if (!sameDay) return false;
      if (_filter == 'Manual') return e.status == 'manual';
      if (_filter == 'Auto')   return e.status != 'manual';
      return true;
    }).toList();
  }

  Widget _buildEmpty() => Center(
    child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
      Icon(Icons.history, size: 64, color: Colors.grey),
      SizedBox(height: 16),
      Text('No history for this date',
          style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
    ]),
  );

  Widget _buildList(List<HistoryEntry> entries) {
    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: entries.length,
      itemBuilder: (_, i) => _HistoryTile(entry: entries[i]),
    );
  }
}

class _FilterTab extends StatelessWidget {
  final String label;
  final bool selected;
  final VoidCallback onTap;
  const _FilterTab(this.label, {required this.selected, required this.onTap});
  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
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

class _HistoryTile extends StatelessWidget {
  final HistoryEntry entry;
  const _HistoryTile({required this.entry});

  Color get _statusColor => switch (entry.status) {
        'completed' => const Color(0xFF4CAF50),
        'manual'    => const Color(0xFF2196F3),
        'paused'    => const Color(0xFFFF9800),
        _           => Colors.grey,
      };

  String get _modeName => switch (entry.mode) {
        OperationMode.literBased      => 'Liter Based',
        OperationMode.timeBased       => 'Time Based',
        OperationMode.timeWindowLiter => 'Time Window + Liter',
      };

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.04),
            blurRadius: 6, offset: const Offset(0, 2))],
      ),
      child: Row(children: [
        SizedBox(
          width: 60,
          child: Text(DateFormat('hh:mm a').format(entry.dateTime),
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
        ),
        Expanded(child: Column(
            crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(entry.cycleName.isEmpty
              ? (entry.status == 'manual'
                  ? 'Manual Use' : 'Cycle ${entry.cycleId} ($_modeName)')
              : '${entry.cycleName} ($_modeName)',
              style: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13)),
          const SizedBox(height: 2),
          Text(entry.status[0].toUpperCase() + entry.status.substring(1),
              style: TextStyle(color: _statusColor, fontSize: 12,
                  fontWeight: FontWeight.w500)),
        ])),
        Column(crossAxisAlignment: CrossAxisAlignment.end, children: [
          Text('${entry.litersDelivered.toStringAsFixed(1)} L',
              style: const TextStyle(fontWeight: FontWeight.bold,
                  color: Color(0xFF2196F3))),
          Text(entry.durationStr,
              style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11)),
        ]),
      ]),
    );
  }
}

class _TotalItem extends StatelessWidget {
  final String label, value;
  final Color color;
  const _TotalItem({required this.label, required this.value,
                    required this.color});
  @override
  Widget build(BuildContext context) => Column(children: [
    Text(label, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
    Text(value, style: TextStyle(color: color, fontWeight: FontWeight.bold,
        fontSize: 18)),
  ]);
}
