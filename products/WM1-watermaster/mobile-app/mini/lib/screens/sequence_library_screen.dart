import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/providers.dart';
import '../models/program.dart';
import '../models/device_status.dart';
import 'sequence_editor_screen.dart';

const _kLibraryCacheKeyPrefix = 'cached_library_';

/// The shared Sequence "superset" — reusable templates a Program is
/// built FROM (§ user request: "create superset of the seq and select
/// few based on the req"). Picking one from here while editing a
/// Program copies its data in; editing/deleting an entry here never
/// retroactively changes a program that already copied it — see
/// firmware's SequenceLibrary.h for why.
class SequenceLibraryScreen extends ConsumerStatefulWidget {
  const SequenceLibraryScreen({super.key});
  @override
  ConsumerState<SequenceLibraryScreen> createState() => _SequenceLibraryScreenState();
}

class _SequenceLibraryScreenState extends ConsumerState<SequenceLibraryScreen> {
  List<LibrarySequence> _entries = [];
  bool _loading = true;
  bool _didInitialRequest = false;

  @override
  void initState() {
    super.initState();
    _loadFromCache();
  }

  Future<void> _loadFromCache() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_kLibraryCacheKeyPrefix);
    if (raw == null || !mounted) return;
    try {
      final list = (jsonDecode(raw) as List).map((e) => LibrarySequence.fromJson(e as Map<String, dynamic>)).toList();
      if (mounted && _entries.isEmpty) setState(() { _entries = list; _loading = false; });
    } catch (_) {}
  }

  Future<void> _saveToCache(List<LibrarySequence> entries) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_kLibraryCacheKeyPrefix, jsonEncode(entries.map((e) => e.toJson()).toList()));
  }

  void _requestLibrary() {
    if (_entries.isEmpty) setState(() => _loading = true);
    ref.read(deviceServiceProvider).getLibrary();
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted && _loading) setState(() => _loading = false);
    });
  }

  void _push(List<Sequence> sequences) {
    final entries = sequences.map((s) => LibrarySequence(sequence: s)).toList();
    setState(() => _entries = entries);
    _saveToCache(entries);
    ref.read(deviceServiceProvider).setLibrary(entries);
  }

  Future<void> _openEditor({Sequence? sequence, int? index, required RelayNames names}) async {
    final result = await Navigator.push<Sequence>(context,
        MaterialPageRoute(builder: (_) => SequenceEditorScreen(sequence: sequence, relayNames: names)));
    if (result == null) return;
    final updated = _entries.map((e) => e.sequence).toList();
    if (index != null) updated[index] = result; else updated.add(result);
    _push(updated);
  }

  void _delete(int index) {
    final updated = _entries.map((e) => e.sequence).toList()..removeAt(index);
    _push(updated);
  }

  @override
  Widget build(BuildContext context) {
    final connected = ref.watch(deviceConnectedProvider);
    final names = ref.watch(deviceStatusProvider).valueOrNull?.relayNames ?? const RelayNames();

    ref.listen<AsyncValue<List<LibrarySequence>>>(libraryProvider, (previous, next) {
      next.whenData((entries) {
        if (mounted) setState(() { _entries = entries; _loading = false; });
        _saveToCache(entries);
      });
    });

    ref.listen(deviceConnectedProvider, (prev, next) {
      final wasConnected = prev ?? false;
      if (next && !wasConnected) _requestLibrary();
    });
    if (!_didInitialRequest && connected) {
      _didInitialRequest = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _requestLibrary());
    }

    return Scaffold(
      appBar: AppBar(
        title: const Text('Sequence Library', style: TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.add),
            style: IconButton.styleFrom(backgroundColor: Theme.of(context).colorScheme.primary, foregroundColor: Colors.white),
            onPressed: () => _openEditor(names: names),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _entries.isEmpty
              ? _buildEmpty(context, names)
              : _buildList(names),
    );
  }

  Widget _buildEmpty(BuildContext context, RelayNames names) => Center(
        child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
          const Icon(Icons.dashboard_customize_outlined, size: 64, color: Colors.grey),
          const SizedBox(height: 16),
          Text('No saved sequences yet', style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
          const SizedBox(height: 8),
          const Text('Build a set of reusable valve combinations here, then\npick from them when building any program.',
              textAlign: TextAlign.center, style: TextStyle(color: Colors.grey, fontSize: 13)),
          const SizedBox(height: 24),
          ElevatedButton.icon(onPressed: () => _openEditor(names: names), icon: const Icon(Icons.add), label: const Text('Add Sequence')),
        ]),
      );

  Widget _buildList(RelayNames names) => RefreshIndicator(
        onRefresh: () async => _requestLibrary(),
        child: ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: _entries.length,
          itemBuilder: (_, i) {
            final seq = _entries[i].sequence;
            final valveLabels = [for (int v = 0; v < 4; v++) if (seq.valveOn(v)) names.valveName(v)].join(', ');
            final duration = seq.runMode == RunMode.time ? '${seq.runTargetSec ~/ 60}m' : '${seq.runTargetLiters} L';
            return Dismissible(
              key: ValueKey('${seq.name}_$i'),
              direction: DismissDirection.endToStart,
              background: Container(
                alignment: Alignment.centerRight,
                padding: const EdgeInsets.only(right: 24),
                margin: const EdgeInsets.only(bottom: 12),
                decoration: BoxDecoration(color: Colors.red, borderRadius: BorderRadius.circular(12)),
                child: const Icon(Icons.delete, color: Colors.white),
              ),
              confirmDismiss: (_) => showDialog<bool>(
                context: context,
                builder: (ctx) => AlertDialog(
                  title: const Text('Delete Sequence?'),
                  content: Text('Delete "${seq.name}" from the library? Programs that already copied it are unaffected.'),
                  actions: [
                    TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
                    TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Delete', style: TextStyle(color: Colors.red))),
                  ],
                ),
              ).then((c) => c ?? false),
              onDismissed: (_) => _delete(i),
              child: Container(
                margin: const EdgeInsets.only(bottom: 12),
                decoration: BoxDecoration(
                  color: Theme.of(context).cardColor,
                  borderRadius: BorderRadius.circular(12),
                  boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
                ),
                child: ListTile(
                  onTap: () => _openEditor(sequence: seq, index: i, names: names),
                  title: Text(seq.name, style: const TextStyle(fontWeight: FontWeight.w600)),
                  subtitle: Text('${valveLabels.isEmpty ? "no valves" : valveLabels} • $duration${seq.doseEnabled ? " • dosing" : ""}',
                      style: const TextStyle(fontSize: 12, color: Colors.grey)),
                  trailing: const Icon(Icons.chevron_right, color: Colors.grey),
                ),
              ),
            );
          },
        ),
      );
}
