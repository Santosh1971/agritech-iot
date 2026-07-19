import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/providers.dart';
import '../models/cycle.dart';
import 'add_cycle_screen.dart';

const String _kCyclesCacheKey = 'cached_cycles';

class CyclesScreen extends ConsumerStatefulWidget {
  const CyclesScreen({super.key});
  @override
  ConsumerState<CyclesScreen> createState() => _CyclesScreenState();
}

class _CyclesScreenState extends ConsumerState<CyclesScreen> {
  // Local cycles list — updated from cache, then device push + local edits
  List<Cycle> _cycles = [];
  bool _loading = true;
  bool _didInitialRequest = false;

  @override
  void initState() {
    super.initState();
    _loadFromCache();
    // NOTE: no eager _requestCycles() here anymore — with all screens now
    // built at once (IndexedStack), this used to fire before the
    // connection was actually established and got silently dropped, with
    // nothing to retry it. See the ref.listen in build() instead, which
    // reacts to the connection actually coming up.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      ref.read(deviceServiceProvider).cyclesStream.listen((cycles) {
        if (mounted) setState(() { _cycles = cycles; _loading = false; });
        _saveToCache(cycles);
      });
    });
  }

  Future<void> _loadFromCache() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_kCyclesCacheKey);
    if (raw == null || !mounted) return;
    try {
      final list = (jsonDecode(raw) as List)
          .map((e) => Cycle.fromJson(e as Map<String, dynamic>))
          .toList();
      // Only apply cached data if we haven't already received something live
      if (mounted && _cycles.isEmpty) {
        setState(() { _cycles = list; _loading = false; });
      }
    } catch (_) {
      // Corrupt cache — ignore, MQTT will repopulate
    }
  }

  Future<void> _saveToCache(List<Cycle> cycles) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = jsonEncode(cycles.map((c) => c.toJson()).toList());
    await prefs.setString(_kCyclesCacheKey, raw);
  }

  void _requestCycles() {
    // Only show the spinner if we have nothing to show yet (no cache hit)
    if (_cycles.isEmpty) setState(() => _loading = true);
    ref.read(deviceServiceProvider).getCycles();
    // Timeout loading after 3 seconds — fall back to whatever we have (cache or empty)
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted && _loading) setState(() => _loading = false);
    });
  }

  Future<void> _openAddCycle({Cycle? cycle}) async {
    await Navigator.push(context,
        MaterialPageRoute(
            builder: (_) => AddCycleScreen(
                  cycle: cycle,
                  existingCycles: _cycles,
                  onSaved: (updatedCycles) {
                    setState(() => _cycles = updatedCycles);
                    _saveToCache(updatedCycles);
                    // Send to device
                    ref.read(deviceServiceProvider).setCycles(updatedCycles);
                  },
                )));
    // Refresh after returning
    _requestCycles();
  }

  void _toggleCycle(int index, bool val) {
    final updated = List<Cycle>.from(_cycles);
    updated[index] = _cycles[index].copyWith(enabled: val);
    setState(() => _cycles = updated);
    _saveToCache(updated);
    ref.read(deviceServiceProvider).setCycles(updated);
  }

  void _deleteCycle(int index) {
    final updated = List<Cycle>.from(_cycles)..removeAt(index);
    setState(() => _cycles = updated);
    _saveToCache(updated);
    ref.read(deviceServiceProvider).setCycles(updated);
  }

  @override
  Widget build(BuildContext context) {
    final connected = ref.watch(deviceConnectedProvider);

    // Re-request cycles the moment the connection is (re)established —
    // covers first launch, reconnects after a transport switch, and
    // WiFi-drop/fallback recovery. Same pattern as the Dashboard screen.
    ref.listen(deviceConnectedProvider, (prev, next) {
      final wasConnected = prev ?? false;
      if (next && !wasConnected) _requestCycles();
    });
    if (!_didInitialRequest && connected) {
      _didInitialRequest = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _requestCycles());
    }

    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        leading: null,
        automaticallyImplyLeading: false,
        title: Text('Cycles',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface,
                fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          if (_cycles.length < 4)
            IconButton(
              icon: const Icon(Icons.add, color: Colors.white),
              style: IconButton.styleFrom(
                  backgroundColor: const Color(0xFF2196F3),
                  shape: const CircleBorder()),
              onPressed: () => _openAddCycle(),
            ),
          const SizedBox(width: 8),
        ],
      ),
      body: _loading
          ? Center(child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                CircularProgressIndicator(color: Color(0xFF2196F3)),
                SizedBox(height: 16),
                Text('Loading cycles...', style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant)),
              ]))
          : _cycles.isEmpty
              ? _buildEmpty()
              : _buildList(),
    );
  }

  Widget _buildEmpty() => Center(
    child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
      const Icon(Icons.loop, size: 64, color: Colors.grey),
      const SizedBox(height: 16),
      Text('No cycles configured',
          style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
      const SizedBox(height: 8),
      Text('Tap + to add your first watering cycle',
          style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 13)),
      const SizedBox(height: 24),
      ElevatedButton.icon(
        onPressed: () => _openAddCycle(),
        icon: const Icon(Icons.add, color: Colors.white),
        label: const Text('Add Cycle',
            style: TextStyle(color: Colors.white)),
        style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFF2196F3),
            padding: const EdgeInsets.symmetric(
                horizontal: 24, vertical: 12)),
      ),
    ]),
  );

  Widget _buildList() => RefreshIndicator(
    onRefresh: () async => _requestCycles(),
    child: ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: _cycles.length,
      itemBuilder: (_, i) => Dismissible(
        key: ValueKey(_cycles[i].id),
        direction: DismissDirection.endToStart,
        background: Container(
          alignment: Alignment.centerRight,
          padding: const EdgeInsets.only(right: 24),
          margin: const EdgeInsets.only(bottom: 12),
          decoration: BoxDecoration(
            color: Colors.red,
            borderRadius: BorderRadius.circular(12)),
          child: const Icon(Icons.delete, color: Colors.white),
        ),
        confirmDismiss: (_) => showDialog<bool>(
          context: context,
          builder: (ctx) => AlertDialog(
            title: const Text('Delete Cycle?'),
            content: Text('Delete "${_cycles[i].name.isEmpty ? "Cycle ${i + 1}" : _cycles[i].name}"? This cannot be undone.'),
            actions: [
              TextButton(onPressed: () => Navigator.pop(ctx, false),
                  child: const Text('Cancel')),
              TextButton(onPressed: () => Navigator.pop(ctx, true),
                  child: const Text('Delete', style: TextStyle(color: Colors.red))),
            ],
          ),
        ).then((confirmed) => confirmed ?? false),
        onDismissed: (_) => _deleteCycle(i),
        child: _CycleTile(
          cycle: _cycles[i],
          index: i + 1,
          onTap: () => _openAddCycle(cycle: _cycles[i]),
          onToggle: (val) => _toggleCycle(i, val),
        ),
      ),
    ),
  );
}

class _CycleTile extends StatelessWidget {
  final Cycle cycle;
  final int index;
  final VoidCallback onTap;
  final ValueChanged<bool> onToggle;
  const _CycleTile({required this.cycle, required this.index,
                    required this.onTap, required this.onToggle});

  Color get _modeColor => switch (cycle.mode) {
        OperationMode.literBased      => const Color(0xFF9C27B0),
        OperationMode.timeBased       => const Color(0xFF4CAF50),
        OperationMode.timeWindowLiter => const Color(0xFF2196F3),
      };

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
            blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: ListTile(
        contentPadding: const EdgeInsets.fromLTRB(16, 8, 8, 8),
        onTap: onTap,
        title: Row(children: [
          Text('Cycle $index',
              style: const TextStyle(
                  fontWeight: FontWeight.w600, fontSize: 15)),
          if (cycle.name.isNotEmpty) ...[
            const SizedBox(width: 8),
            Flexible(
              child: Text('· ${cycle.name}',
                  overflow: TextOverflow.ellipsis,
                  maxLines: 1,
                  style: TextStyle(color: Colors.grey.shade500,
                      fontSize: 13)),
            ),
          ],
          const SizedBox(width: 8),
          Container(
            padding: const EdgeInsets.symmetric(
                horizontal: 8, vertical: 3),
            decoration: BoxDecoration(
              color: _modeColor.withOpacity(0.1),
              borderRadius: BorderRadius.circular(12)),
            child: Text(cycle.modeLabel,
                style: TextStyle(color: _modeColor,
                    fontSize: 11, fontWeight: FontWeight.w600)),
          ),
        ]),
        subtitle: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
          const SizedBox(height: 6),
          Row(children: [
            const Icon(Icons.access_time, size: 14, color: Colors.grey),
            const SizedBox(width: 4),
            Text(cycle.startTimeStr,
                style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 13)),
            const SizedBox(width: 8),
            Text('Everyday',
                style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 12)),
          ]),
          const SizedBox(height: 4),
          Row(children: [
            if (cycle.mode != OperationMode.timeBased) ...[
              const Icon(Icons.water_drop, size: 14,
                  color: Color(0xFF2196F3)),
              const SizedBox(width: 4),
              Text('${cycle.targetLiters.toStringAsFixed(1)} L',
                  style: const TextStyle(color: Color(0xFF2196F3),
                      fontWeight: FontWeight.w600, fontSize: 13)),
              const SizedBox(width: 8),
            ],
            if (cycle.mode != OperationMode.literBased) ...[
              const Icon(Icons.timer_outlined, size: 14,
                  color: Colors.grey),
              const SizedBox(width: 4),
              Text('${cycle.startTimeStr} - ${cycle.endTimeStr}',
                  style: const TextStyle(
                      color: Colors.grey, fontSize: 12)),
            ],
          ]),
        ]),
        trailing: Row(mainAxisSize: MainAxisSize.min, children: [
          Switch(
            value: cycle.enabled,
            onChanged: onToggle,
            activeColor: const Color(0xFF4CAF50),
          ),
          const Icon(Icons.chevron_right, color: Colors.grey),
        ]),
      ),
    );
  }
}
