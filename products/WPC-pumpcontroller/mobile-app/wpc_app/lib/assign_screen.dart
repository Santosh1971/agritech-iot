import 'package:flutter/material.dart';
import 'api.dart';

class AssignScreen extends StatefulWidget {
  const AssignScreen({super.key});

  @override
  State<AssignScreen> createState() => _AssignScreenState();
}

class _AssignScreenState extends State<AssignScreen> {
  Map<String, dynamic>? _status;
  String? _error;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _fetch();
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
      setState(() => _error = 'Not reachable: \$e');
    }
  }

  Future<void> _setNumLevels(int n) async {
    setState(() => _busy = true);
    try {
      await WpcApi.setNumLevels(n);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to update level count: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _renamePump(int slot, String currentName) async {
    final controller = TextEditingController(text: currentName);
    final newName = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Pump name'),
        content: TextField(
          controller: controller,
          autofocus: true,
          decoration: const InputDecoration(hintText: 'e.g. Field Pump A'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          TextButton(
            onPressed: () => Navigator.pop(ctx, controller.text.trim()),
            child: const Text('Save'),
          ),
        ],
      ),
    );
    if (newName == null) return;
    setState(() => _busy = true);
    try {
      await WpcApi.setPumpName(slot, newName);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to rename: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _togglePumpLevel(int slot, int level, bool nowAssigned) async {
    setState(() => _busy = true);
    try {
      await WpcApi.setPumpLevel(slot, level, nowAssigned);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to assign pump: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
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

    final numLevels = (_status!['numLevels'] as num?)?.toInt() ?? 3;
    final pumps = _status!['pumps'] as List<dynamic>? ?? [];

    return RefreshIndicator(
      onRefresh: _fetch,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text('Number of Levels', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          SegmentedButton<int>(
            segments: const [
              ButtonSegment(value: 1, label: Text('1')),
              ButtonSegment(value: 2, label: Text('2')),
              ButtonSegment(value: 3, label: Text('3')),
            ],
            selected: {numLevels},
            onSelectionChanged: _busy
                ? null
                : (selection) => _setNumLevels(selection.first),
          ),
          const SizedBox(height: 24),

          if (pumps.isEmpty)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 24),
              child: Text('No pumps joined yet -- power on a Pump Node to see it here.'),
            ),

          if (pumps.isNotEmpty) ...[
            Text('Available Pumps', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            ...pumps.map((p) {
              final map = p as Map<String, dynamic>;
              final slot = (map['slot'] as num).toInt();
              final name = (map['name'] as String?) ?? '';
              final displayName = name.isNotEmpty ? name : 'Pump ${map['pumpId']}';
              return ListTile(
                dense: true,
                title: Text(displayName),
                trailing: IconButton(
                  icon: const Icon(Icons.edit, size: 20),
                  onPressed: _busy ? null : () => _renamePump(slot, name),
                ),
              );
            }),
            const SizedBox(height: 16),
          ],

          // Top-to-bottom Level N .. Level 1, matching the physical board.
          ...List.generate(numLevels, (i) {
            final level = numLevels - i;
            return Padding(
              padding: const EdgeInsets.only(bottom: 20),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('Level $level', style: Theme.of(context).textTheme.titleMedium),
                  const SizedBox(height: 8),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: pumps.map((p) {
                      final map = p as Map<String, dynamic>;
                      final slot = (map['slot'] as num).toInt();
                      final pumpId = map['pumpId'];
                      final assignedLevels = (map['assignedLevels'] as List<dynamic>? ?? [])
                          .map((e) => (e as num).toInt())
                          .toSet();
                      final selected = assignedLevels.contains(level);
                      final name = (map['name'] as String?) ?? '';
                      final displayName = name.isNotEmpty ? name : 'Pump $pumpId';
                      return FilterChip(
                        label: Text(displayName),
                        selected: selected,
                        onSelected: _busy
                            ? null
                            : (nowSelected) => _togglePumpLevel(slot, level, nowSelected),
                      );
                    }).toList(),
                  ),
                ],
              ),
            );
          }),
        ],
      ),
    );
  }
}
