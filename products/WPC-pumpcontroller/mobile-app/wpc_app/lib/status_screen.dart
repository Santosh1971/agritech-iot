import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'api.dart';

class StatusScreen extends StatefulWidget {
  const StatusScreen({super.key});

  @override
  State<StatusScreen> createState() => _StatusScreenState();
}

class _StatusScreenState extends State<StatusScreen> {
  static const Duration _pollInterval = Duration(seconds: 3);

  Map<String, dynamic>? _status;
  String? _error;
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _fetch();
    _timer = Timer.periodic(_pollInterval, (_) => _fetch());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> _fetch() async {
    try {
      final status = await WpcApi.getStatus();
      if (!mounted) return;
      setState(() {
        _status = status;
        _error = null;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = 'Not reachable: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(onRefresh: _fetch, child: _buildBody());
  }

  Widget _buildBody() {
    if (_error != null) {
      return ListView(
        children: [
          Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              children: [
                const Icon(Icons.wifi_off, size: 48, color: Colors.grey),
                const SizedBox(height: 12),
                Text(_error!, textAlign: TextAlign.center),
              ],
            ),
          ),
        ],
      );
    }

    if (_status == null) {
      return const Center(child: CircularProgressIndicator());
    }

    final masterId = (_status!['masterId'] as String? ?? '--')
        .replaceFirst(RegExp(r'^0x', caseSensitive: false), '');
    final numLevels = (_status!['numLevels'] as num?)?.toInt() ?? 0;
    final levels = (_status!['levels'] as List<dynamic>? ?? []);
    final noPower = _status!['noPower'] == true;
    final pumps = _status!['pumps'] as List<dynamic>? ?? [];

    final unassigned = pumps.where((p) {
      final assigned = ((p as Map<String, dynamic>)['assignedLevels'] as List<dynamic>? ?? []);
      return assigned.isEmpty;
    }).toList();

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Row(
          children: [
            Text('Master: $masterId', style: Theme.of(context).textTheme.bodySmall),
            const SizedBox(width: 4),
            InkWell(
              onTap: () {
                Clipboard.setData(ClipboardData(text: masterId));
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('Master ID copied')),
                );
              },
              child: const Icon(Icons.copy, size: 16, color: Colors.grey),
            ),
          ],
        ),
        const SizedBox(height: 12),

        if (noPower)
          Container(
            padding: const EdgeInsets.all(12),
            margin: const EdgeInsets.only(bottom: 16),
            decoration: BoxDecoration(
              color: Colors.red.shade50,
              border: Border.all(color: Colors.red.shade300),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Row(
              children: [
                Icon(Icons.power_off, color: Colors.red.shade700),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    'No Power detected',
                    style: TextStyle(color: Colors.red.shade700, fontWeight: FontWeight.bold),
                  ),
                ),
              ],
            ),
          ),

        // Levels top-to-bottom as Level N .. Level 1, matching the physical
        // board layout (highest level at top). Each box shows the level's
        // own state plus the pumps assigned to it -- no slot numbers, since
        // that's internal wire-protocol addressing, not something an
        // installer needs to see.
        for (int lvl = numLevels; lvl >= 1; lvl--) ...[
          _buildLevelBox(context, lvl, levels, pumps),
          const SizedBox(height: 12),
        ],

        if (unassigned.isNotEmpty) ...[
          Text('Unassigned', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Colors.grey.shade100,
              border: Border.all(color: Colors.grey.shade300),
              borderRadius: BorderRadius.circular(10),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: unassigned.map((p) => _pumpRow(p as Map<String, dynamic>)).toList(),
            ),
          ),
        ],
      ],
    );
  }

  Widget _buildLevelBox(
      BuildContext context, int lvl, List<dynamic> levels, List<dynamic> pumps) {
    final active = (lvl - 1) < levels.length && levels[lvl - 1] == true;
    final levelPumps = pumps.where((p) {
      final assigned = ((p as Map<String, dynamic>)['assignedLevels'] as List<dynamic>? ?? [])
          .map((e) => (e as num).toInt());
      return assigned.contains(lvl);
    }).toList();

    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: active ? Colors.amber.shade50 : Colors.grey.shade100,
        border: Border.all(color: active ? Colors.amber.shade300 : Colors.grey.shade300),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                active ? Icons.water_drop : Icons.water_drop_outlined,
                color: active ? Colors.amber.shade800 : Colors.grey,
              ),
              const SizedBox(width: 8),
              Text('Level $lvl', style: Theme.of(context).textTheme.titleMedium),
            ],
          ),
          const SizedBox(height: 8),
          if (levelPumps.isEmpty)
            const Text('No pumps assigned', style: TextStyle(color: Colors.grey)),
          ...levelPumps.map((p) => _pumpRow(p as Map<String, dynamic>)),
        ],
      ),
    );
  }

  Widget _pumpRow(Map<String, dynamic> map) {
    final online = map['online'] == true;
    final relay = map['relay'] == true;
    final name = (map['name'] as String?) ?? '';
    final displayName = name.isNotEmpty ? name : 'Pump ${map['pumpId']}';
    return Padding(
      padding: const EdgeInsets.only(top: 4),
      child: Row(
        children: [
          Icon(
            relay ? Icons.power : Icons.power_off,
            size: 18,
            color: !online ? Colors.red.shade300 : (relay ? Colors.green : Colors.grey),
          ),
          const SizedBox(width: 6),
          Expanded(child: Text(displayName)),
          Text(
            !online ? 'Offline' : (relay ? 'Running' : 'Idle'),
            style: TextStyle(
              color: !online ? Colors.red : (relay ? Colors.green.shade700 : Colors.grey),
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ),
    );
  }
}
