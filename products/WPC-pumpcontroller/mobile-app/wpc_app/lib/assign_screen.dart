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
  double? _debounceSliderValue;   // local while dragging; null = show the server's current value
  double? _txPowerSliderValue;    // local while dragging; null = show the server's current value

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
      setState(() => _error = 'Not reachable: $e');
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

  Future<void> _forgetPump(int slot, String displayName) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Forget pump?'),
        content: Text(
          '$displayName will be removed from the Master and no longer show anywhere. '
          "This doesn't affect the physical Pump Node -- it can rejoin later if it's still active.",
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Forget', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    setState(() => _busy = true);
    try {
      await WpcApi.forgetPump(slot);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to forget: $e')),
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

          Text('Level Debounce Time', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 4),
          Builder(builder: (context) {
            final debounceMs = (_status!['debounceMs'] as num?)?.toInt() ?? 10000;
            final serverSeconds = (debounceMs / 1000).clamp(10.0, 300.0);
            final displaySeconds = _debounceSliderValue ?? serverSeconds;
            return Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('${displaySeconds.round()}s', style: Theme.of(context).textTheme.bodyMedium),
                Slider(
                  min: 10,
                  max: 300,
                  divisions: 29,
                  value: displaySeconds,
                  onChanged: _busy
                      ? null
                      : (v) => setState(() => _debounceSliderValue = v),
                  onChangeEnd: _busy
                      ? null
                      : (v) async {
                          setState(() => _busy = true);
                          try {
                            await WpcApi.setDebounceMs((v * 1000).round());
                            await _fetch();
                          } catch (e) {
                            if (mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(content: Text('Failed to set debounce: $e')),
                              );
                            }
                          } finally {
                            if (mounted) {
                              setState(() {
                                _busy = false;
                                _debounceSliderValue = null;
                              });
                            }
                          }
                        },
                ),
              ],
            );
          }),
          const SizedBox(height: 24),

          Text('Master Radio TX Power', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 2),
          Text(
            // Only affects what THIS Master transmits -- range depends on
            // both ends, so each Pump's own TX power (Provision screen,
            // while connected to that Pump's SoftAP) needs raising too.
            'Higher = longer range, more airtime/battery use. Set each Pump separately.',
            style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
          ),
          const SizedBox(height: 4),
          Builder(builder: (context) {
            final serverDbm = ((_status!['txPower'] as num?)?.toDouble() ?? 14).clamp(-9.0, 22.0);
            final displayDbm = _txPowerSliderValue ?? serverDbm;
            return Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('${displayDbm.round()} dBm', style: Theme.of(context).textTheme.bodyMedium),
                Slider(
                  min: -9,
                  max: 22,
                  divisions: 31,
                  value: displayDbm,
                  onChanged: _busy
                      ? null
                      : (v) => setState(() => _txPowerSliderValue = v),
                  onChangeEnd: _busy
                      ? null
                      : (v) async {
                          setState(() => _busy = true);
                          try {
                            await WpcApi.setTxPower(v.round());
                            await _fetch();
                          } catch (e) {
                            if (mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(content: Text('Failed to set TX power: $e')),
                              );
                            }
                          } finally {
                            if (mounted) {
                              setState(() {
                                _busy = false;
                                _txPowerSliderValue = null;
                              });
                            }
                          }
                        },
                ),
              ],
            );
          }),
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
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    IconButton(
                      icon: const Icon(Icons.edit, size: 20),
                      onPressed: _busy ? null : () => _renamePump(slot, name),
                    ),
                    IconButton(
                      icon: const Icon(Icons.delete_outline, size: 20, color: Colors.red),
                      onPressed: _busy ? null : () => _forgetPump(slot, displayName),
                    ),
                  ],
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
