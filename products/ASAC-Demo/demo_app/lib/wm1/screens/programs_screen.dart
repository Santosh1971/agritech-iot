import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../providers/providers.dart';
import '../models/program.dart';
import 'program_editor_screen.dart';
import 'sequence_library_screen.dart';

const _kProgramsCacheKeyPrefix = 'cached_programs_';

class ProgramsScreen extends ConsumerStatefulWidget {
  const ProgramsScreen({super.key});
  @override
  ConsumerState<ProgramsScreen> createState() => _ProgramsScreenState();
}

class _ProgramsScreenState extends ConsumerState<ProgramsScreen> {
  List<Program> _programs = [];
  bool _loading = true;
  bool _didInitialRequest = false;

  @override
  void initState() {
    super.initState();
    _loadFromCache();
  }

  String _cacheKey() => '$_kProgramsCacheKeyPrefix${ref.read(deviceSuffixProvider)}';

  Future<void> _loadFromCache() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_cacheKey());
    if (raw == null || !mounted) return;
    try {
      final list = (jsonDecode(raw) as List).map((e) => Program.fromJson(e as Map<String, dynamic>)).toList();
      if (mounted && _programs.isEmpty) setState(() { _programs = list; _loading = false; });
    } catch (_) {}
  }

  Future<void> _saveToCache(List<Program> programs) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_cacheKey(), jsonEncode(programs.map((p) => p.toJson()).toList()));
  }

  void _requestPrograms() {
    if (_programs.isEmpty) setState(() => _loading = true);
    ref.read(deviceServiceProvider).getPrograms();
    Future.delayed(const Duration(seconds: 3), () {
      if (mounted && _loading) setState(() => _loading = false);
    });
  }

  void _push(List<Program> updated) {
    setState(() => _programs = updated);
    _saveToCache(updated);
    ref.read(deviceServiceProvider).setPrograms(updated);
  }

  Future<void> _openEditor({Program? program, int? index}) async {
    final result = await Navigator.push<Program>(context,
        MaterialPageRoute(builder: (_) => ProgramEditorScreen(program: program)));
    if (result == null) return;
    final updated = List<Program>.from(_programs);
    if (index != null) updated[index] = result; else updated.add(result);
    _push(updated);
  }

  void _toggleEnabled(int index, bool val) {
    final updated = List<Program>.from(_programs);
    updated[index] = updated[index]..enabled = val;
    _push(updated);
  }

  void _delete(int index) {
    final updated = List<Program>.from(_programs)..removeAt(index);
    _push(updated);
  }

  void _runNow(Program p, int seqIndex) {
    ref.read(deviceServiceProvider).triggerProgram(p.id, seqIndex);
    ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Running "${p.sequences[seqIndex].name}" now')));
  }

  @override
  Widget build(BuildContext context) {
    final connected = ref.watch(deviceConnectedProvider);

    ref.listen<AsyncValue<List<Program>>>(programsProvider, (previous, next) {
      next.whenData((programs) {
        if (mounted) setState(() { _programs = programs; _loading = false; });
        _saveToCache(programs);
      });
    });

    ref.listen(deviceConnectedProvider, (prev, next) {
      final wasConnected = prev ?? false;
      if (next && !wasConnected) _requestPrograms();
    });
    if (!_didInitialRequest && connected) {
      _didInitialRequest = true;
      WidgetsBinding.instance.addPostFrameCallback((_) => _requestPrograms());
    }

    return Scaffold(
      appBar: AppBar(
        title: const Text('Programs', style: TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          IconButton(
            icon: const Icon(Icons.dashboard_customize_outlined),
            tooltip: 'Sequence Library',
            onPressed: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const SequenceLibraryScreen())),
          ),
          IconButton(
            icon: const Icon(Icons.add),
            style: IconButton.styleFrom(backgroundColor: Theme.of(context).colorScheme.primary, foregroundColor: Colors.white),
            onPressed: () => _openEditor(),
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : _programs.isEmpty
              ? _buildEmpty(context)
              : _buildList(),
    );
  }

  Widget _buildEmpty(BuildContext context) => Center(
        child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
          const Icon(Icons.calendar_month_outlined, size: 64, color: Colors.grey),
          const SizedBox(height: 16),
          Text('No programs yet', style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
          const SizedBox(height: 8),
          const Text('A program is a named schedule — one or more valve\ncombinations, each with its own duration.',
              textAlign: TextAlign.center, style: TextStyle(color: Colors.grey, fontSize: 13)),
          const SizedBox(height: 24),
          ElevatedButton.icon(onPressed: () => _openEditor(), icon: const Icon(Icons.add), label: const Text('Add Program')),
        ]),
      );

  Widget _buildList() => RefreshIndicator(
        onRefresh: () async => _requestPrograms(),
        child: ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: _programs.length,
          itemBuilder: (_, i) => _ProgramTile(
            program: _programs[i],
            onTap: () => _openEditor(program: _programs[i], index: i),
            onToggle: (v) => _toggleEnabled(i, v),
            onDelete: () => _delete(i),
            onRunSequence: (seqIdx) => _runNow(_programs[i], seqIdx),
          ),
        ),
      );
}

class _ProgramTile extends StatelessWidget {
  final Program program;
  final VoidCallback onTap;
  final ValueChanged<bool> onToggle;
  final VoidCallback onDelete;
  final ValueChanged<int> onRunSequence;
  const _ProgramTile({required this.program, required this.onTap, required this.onToggle, required this.onDelete, required this.onRunSequence});

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 12),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Column(children: [
        ListTile(
          onTap: onTap,
          title: Text(program.name, style: const TextStyle(fontWeight: FontWeight.w600)),
          subtitle: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const SizedBox(height: 4),
            Row(children: [
              const Icon(Icons.access_time, size: 14, color: Colors.grey),
              const SizedBox(width: 4),
              Text(program.startTimeStr, style: const TextStyle(fontSize: 13, color: Colors.grey)),
              const SizedBox(width: 8),
              Text(program.repeatLabel, style: const TextStyle(fontSize: 12, color: Colors.grey)),
            ]),
            const SizedBox(height: 2),
            Row(children: [
              Text('${program.sequences.length} sequence(s)', style: const TextStyle(fontSize: 12, color: Colors.grey)),
              const SizedBox(width: 8),
              // Surfaces autoStart directly on the list — a program with
              // this off will NEVER fire on its own schedule (manual Run
              // Now only), which is easy to miss when just editing the
              // time/sequences of a program that already had it off.
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
                decoration: BoxDecoration(
                  color: (program.autoStart ? Colors.green : Colors.orange).withOpacity(0.12),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  program.autoStart ? 'Auto' : 'Manual only',
                  style: TextStyle(
                      fontSize: 10, fontWeight: FontWeight.w600,
                      color: program.autoStart ? Colors.green.shade700 : Colors.orange.shade800),
                ),
              ),
            ]),
          ]),
          trailing: Row(mainAxisSize: MainAxisSize.min, children: [
            Switch(value: program.enabled, onChanged: onToggle),
            IconButton(icon: const Icon(Icons.delete_outline, color: Colors.red), onPressed: () => _confirmDelete(context)),
          ]),
        ),
        const Divider(height: 1, indent: 16, endIndent: 16),
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 4, 16, 10),
          child: Wrap(spacing: 8, runSpacing: 8, children: [
            for (int i = 0; i < program.sequences.length; i++)
              ActionChip(
                avatar: const Icon(Icons.play_arrow, size: 16),
                label: Text('Run "${program.sequences[i].name}"'),
                onPressed: () => onRunSequence(i),
              ),
          ]),
        ),
      ]),
    );
  }

  void _confirmDelete(BuildContext context) {
    showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Delete Program?'),
        content: Text('Delete "${program.name}"? This cannot be undone.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Delete', style: TextStyle(color: Colors.red))),
        ],
      ),
    ).then((confirmed) { if (confirmed == true) onDelete(); });
  }
}
