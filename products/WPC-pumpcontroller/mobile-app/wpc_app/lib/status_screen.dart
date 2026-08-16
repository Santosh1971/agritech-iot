import 'dart:async';
import 'package:flutter/material.dart';
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
      setState(() => _error = 'Not reachable -- check WiFi is on WPC-Master-XXXX');
    }
  }

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: _fetch,
      child: _buildBody(),
    );
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

    final masterId = _status!['masterId'] as String? ?? '--';
    final numLevels = (_status!['numLevels'] as num?)?.toInt() ?? 0;
    final levels = (_status!['levels'] as List<dynamic>? ?? []);
    final noPower = _status!['noPower'] == true;
    final pumps = _status!['pumps'] as List<dynamic>? ?? [];

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Text('Master: $masterId', style: Theme.of(context).textTheme.bodySmall),
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

        Text('Water Levels', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          runSpacing: 8,
          children: List.generate(numLevels, (i) {
            final active = i < levels.length && levels[i] == true;
            return Chip(
              label: Text('Level ${i + 1}'),
              backgroundColor: active ? Colors.amber.shade100 : Colors.grey.shade200,
              avatar: Icon(
                active ? Icons.water_drop : Icons.water_drop_outlined,
                size: 18,
                color: active ? Colors.amber.shade800 : Colors.grey,
              ),
            );
          }),
        ),
        const SizedBox(height: 24),

        Text('Pumps', style: Theme.of(context).textTheme.titleMedium),
        const SizedBox(height: 8),
        if (pumps.isEmpty)
          const Padding(
            padding: EdgeInsets.symmetric(vertical: 16),
            child: Text('No pumps joined yet.'),
          ),
        ...pumps.map((p) {
          final map = p as Map<String, dynamic>;
          final online = map['online'] == true;
          final relay = map['relay'] == true;
          final assignedLevels = (map['assignedLevels'] as List<dynamic>? ?? [])
              .map((e) => (e as num).toInt())
              .toList();
          final name = (map['name'] as String?) ?? '';
          final displayName = name.isNotEmpty ? name : 'Pump ${map['pumpId']}';
          return Card(
            child: ListTile(
              leading: CircleAvatar(
                backgroundColor: online
                    ? (relay ? Colors.green : Colors.grey.shade400)
                    : Colors.red.shade200,
                child: Icon(
                  relay ? Icons.power : Icons.power_off,
                  color: Colors.white,
                  size: 20,
                ),
              ),
              title: Text('$displayName  (slot ${map['slot']})'),
              subtitle: Text(online
                  ? (relay ? 'Running' : 'Idle')
                  : 'Offline -- not responding'),
              trailing: Chip(
                label: Text(assignedLevels.isEmpty
                    ? 'Unassigned'
                    : 'Levels ${assignedLevels.join(", ")}'),
                backgroundColor:
                    assignedLevels.isEmpty ? Colors.grey.shade200 : Colors.teal.shade50,
              ),
            ),
          );
        }),
      ],
    );
  }
}
