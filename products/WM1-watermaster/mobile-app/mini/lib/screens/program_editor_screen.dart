import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../models/program.dart';
import '../models/device_status.dart';
import '../providers/providers.dart';

class ProgramEditorScreen extends ConsumerStatefulWidget {
  final Program? program;
  const ProgramEditorScreen({super.key, this.program});

  @override
  ConsumerState<ProgramEditorScreen> createState() => _ProgramEditorScreenState();
}

class _ProgramEditorScreenState extends ConsumerState<ProgramEditorScreen> {
  late final TextEditingController _nameController;
  late int _id;
  late bool _enabled;
  late RepeatMode _repeatMode;
  late int _intervalDays;
  late TimeOfDay _startTime;
  late List<Sequence> _sequences;

  @override
  void initState() {
    super.initState();
    final p = widget.program;
    _id = p?.id ?? 0;
    _nameController = TextEditingController(text: p?.name ?? '');
    _enabled = p?.enabled ?? true;
    _repeatMode = p?.repeatMode ?? RepeatMode.interval;
    _intervalDays = p?.intervalDays ?? 1;
    _startTime = TimeOfDay(hour: p?.startHour ?? 6, minute: p?.startMinute ?? 0);
    _sequences = (p?.sequences ?? [Sequence(name: 'Sequence 1')]).map((s) => s.copy()).toList();
    // Refresh the library in the background so "From Library" has
    // up-to-date entries by the time it's tapped, without blocking this
    // screen on a round-trip first.
    WidgetsBinding.instance.addPostFrameCallback((_) => ref.read(deviceServiceProvider).getLibrary());
  }

  @override
  void dispose() {
    _nameController.dispose();
    super.dispose();
  }

  Future<void> _pickTime() async {
    final picked = await showTimePicker(context: context, initialTime: _startTime);
    if (picked != null) setState(() => _startTime = picked);
  }

  void _addSequence() {
    setState(() => _sequences.add(Sequence(name: 'Sequence ${_sequences.length + 1}')));
  }

  Future<void> _addFromLibrary() async {
    final library = ref.read(libraryProvider).valueOrNull ?? const [];
    if (library.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Library is empty — add sequences to it from the Programs screen first')));
      return;
    }
    final picked = await showModalBottomSheet<LibrarySequence>(
      context: context,
      builder: (ctx) => SafeArea(
        child: ListView(
          shrinkWrap: true,
          children: [
            const Padding(
              padding: EdgeInsets.all(16),
              child: Text('Pick from Library', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 16)),
            ),
            for (final entry in library)
              ListTile(
                title: Text(entry.sequence.name),
                subtitle: Text(
                    '${entry.sequence.runMode == RunMode.time ? "${entry.sequence.runTargetSec ~/ 60}m" : "${entry.sequence.runTargetLiters}L"}'
                    '${entry.sequence.doseEnabled ? " • dosing" : ""}'),
                onTap: () => Navigator.pop(ctx, entry),
              ),
          ],
        ),
      ),
    );
    if (picked != null) {
      // Copy semantics — see SequenceLibrary.h: this sequence now
      // belongs to the program independently of the library entry.
      setState(() => _sequences.add(picked.sequence.copy()));
    }
  }

  void _removeSequence(int i) {
    if (_sequences.length <= 1) return;
    setState(() => _sequences.removeAt(i));
  }

  void _save() {
    if (_nameController.text.trim().isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Give the program a name')));
      return;
    }
    if (_sequences.every((s) => s.valveMask == 0)) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Every sequence needs at least one valve selected')));
      return;
    }
    final program = Program(
      id: _id,
      name: _nameController.text.trim(),
      enabled: _enabled,
      // No separate "auto-start" concept in the UI — the spec only ever
      // called for a single enabled/disabled toggle, and having two
      // switches that both had to be on for a schedule to actually fire
      // was a real, repeated source of confusion (a program edited
      // without ever touching this second switch silently stayed
      // manual-only). Always true here; `enabled` is the only thing
      // that gates whether a program runs on its own schedule now.
      autoStart: true,
      repeatMode: _repeatMode,
      intervalDays: _intervalDays,
      startHour: _startTime.hour,
      startMinute: _startTime.minute,
      sequences: _sequences,
    );
    Navigator.pop(context, program);
  }

  @override
  Widget build(BuildContext context) {
    final relayNames = ref.watch(deviceStatusProvider).valueOrNull?.relayNames ?? const RelayNames();
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.program != null ? 'Edit Program' : 'Add Program', style: const TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [
          TextButton(onPressed: _save, child: const Text('Save', style: TextStyle(fontWeight: FontWeight.w600))),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _Card(children: [
            _Label('Program Name'),
            TextField(controller: _nameController, decoration: _dec('e.g. Morning Cycle')),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            _Label('Start Time'),
            InkWell(
              onTap: _pickTime,
              borderRadius: BorderRadius.circular(8),
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 14),
                decoration: BoxDecoration(border: Border.all(color: Colors.grey.shade400), borderRadius: BorderRadius.circular(8)),
                child: Row(children: [
                  Text(_startTime.format(context), style: const TextStyle(fontSize: 16)),
                  const Spacer(),
                  const Icon(Icons.access_time, color: Colors.grey, size: 20),
                ]),
              ),
            ),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            _Label('Repeat'),
            Row(children: [
              Expanded(
                child: RadioListTile<RepeatMode>(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('Interval', style: TextStyle(fontSize: 14)),
                  value: RepeatMode.interval,
                  groupValue: _repeatMode,
                  onChanged: (v) => setState(() => _repeatMode = v!),
                ),
              ),
              Expanded(
                child: RadioListTile<RepeatMode>(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('Rotation', style: TextStyle(fontSize: 14)),
                  value: RepeatMode.rotation,
                  groupValue: _repeatMode,
                  onChanged: (v) => setState(() => _repeatMode = v!),
                ),
              ),
            ]),
            if (_repeatMode == RepeatMode.interval) ...[
              const SizedBox(height: 4),
              Row(children: [
                const Text('Every'),
                const SizedBox(width: 8),
                SizedBox(
                  width: 70,
                  child: TextFormField(
                    initialValue: _intervalDays.toString(),
                    keyboardType: TextInputType.number,
                    decoration: _dec(''),
                    onChanged: (v) => _intervalDays = int.tryParse(v) ?? 1,
                  ),
                ),
                const SizedBox(width: 8),
                const Text('day(s) — 1 = daily, 2 = alternate-day, ...'),
              ]),
            ] else
              Padding(
                padding: const EdgeInsets.only(top: 4),
                child: Text('One sequence runs per day, cycling through the list below in order.',
                    style: TextStyle(color: Colors.grey.shade600, fontSize: 12)),
              ),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            Row(children: [
              const Text('Enabled', style: TextStyle(fontWeight: FontWeight.w500)),
              const Spacer(),
              Switch(value: _enabled, onChanged: (v) => setState(() => _enabled = v)),
            ]),
          ]),
          const SizedBox(height: 20),
          Row(children: [
            const Text('SEQUENCES', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w600, letterSpacing: 0.5, color: Colors.grey)),
            const Spacer(),
            TextButton.icon(onPressed: _addFromLibrary, icon: const Icon(Icons.dashboard_customize_outlined, size: 18), label: const Text('From Library')),
            TextButton.icon(onPressed: _addSequence, icon: const Icon(Icons.add, size: 18), label: const Text('Blank')),
          ]),
          const SizedBox(height: 4),
          Text('Each sequence is its own valve combination and duration. They run one after another when this program triggers.',
              style: TextStyle(color: Colors.grey.shade600, fontSize: 12)),
          const SizedBox(height: 12),
          for (int i = 0; i < _sequences.length; i++)
            Padding(
              padding: const EdgeInsets.only(bottom: 12),
              child: _SequenceCard(
                index: i,
                sequence: _sequences[i],
                relayNames: relayNames,
                canDelete: _sequences.length > 1,
                onDelete: () => _removeSequence(i),
                onChanged: () => setState(() {}),
              ),
            ),
          const SizedBox(height: 24),
        ],
      ),
    );
  }

  InputDecoration _dec(String hint) => InputDecoration(
        hintText: hint,
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
        isDense: true,
      );
}

class _SequenceCard extends StatefulWidget {
  final int index;
  final Sequence sequence;
  final RelayNames relayNames;
  final bool canDelete;
  final VoidCallback onDelete;
  final VoidCallback onChanged;
  const _SequenceCard({required this.index, required this.sequence, required this.relayNames, required this.canDelete, required this.onDelete, required this.onChanged});

  @override
  State<_SequenceCard> createState() => _SequenceCardState();
}

class _SequenceCardState extends State<_SequenceCard> {
  late final TextEditingController _nameController;
  late final TextEditingController _durationController;
  late final TextEditingController _litersController;
  late final TextEditingController _doseDurationController;

  Sequence get s => widget.sequence;

  @override
  void initState() {
    super.initState();
    _nameController = TextEditingController(text: s.name);
    _durationController = TextEditingController(text: (s.runTargetSec ~/ 60).toString());
    _litersController = TextEditingController(text: s.runTargetLiters.toString());
    _doseDurationController = TextEditingController(text: (s.doseDurationSec ~/ 60).toString());
  }

  @override
  void dispose() {
    _nameController.dispose();
    _durationController.dispose();
    _litersController.dispose();
    _doseDurationController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Theme.of(context).cardColor,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.grey.shade300),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          Expanded(
            child: TextField(
              controller: _nameController,
              decoration: const InputDecoration(border: InputBorder.none, hintText: 'Sequence name', isDense: true),
              style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 15),
              onChanged: (v) { s.name = v; widget.onChanged(); },
            ),
          ),
          if (widget.canDelete)
            IconButton(icon: const Icon(Icons.close, size: 20, color: Colors.grey), onPressed: widget.onDelete),
        ]),
        const SizedBox(height: 8),
        const Text('VALVES', style: TextStyle(fontSize: 10, fontWeight: FontWeight.w600, color: Colors.grey, letterSpacing: 0.5)),
        const SizedBox(height: 6),
        Wrap(spacing: 8, children: [
          for (int v = 0; v < 4; v++)
            FilterChip(
              label: Text(widget.relayNames.valveName(v)),
              selected: s.valveOn(v),
              onSelected: (sel) { setState(() { s.setValve(v, sel); }); widget.onChanged(); },
              selectedColor: const Color(0xFF2196F3).withOpacity(0.2),
              checkmarkColor: const Color(0xFF1565C0),
            ),
        ]),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(
            child: DropdownButtonFormField<RunMode>(
              value: s.runMode,
              decoration: const InputDecoration(labelText: 'Run mode', isDense: true, border: OutlineInputBorder()),
              items: const [
                DropdownMenuItem(value: RunMode.time, child: Text('Time-based')),
                DropdownMenuItem(value: RunMode.volume, child: Text('Volume-based')),
              ],
              onChanged: (v) { setState(() { s.runMode = v!; }); widget.onChanged(); },
            ),
          ),
          const SizedBox(width: 8),
          if (s.runMode == RunMode.time)
            Expanded(
              child: TextField(
                controller: _durationController,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(labelText: 'Minutes', isDense: true, border: OutlineInputBorder()),
                onChanged: (v) { s.runTargetSec = (int.tryParse(v) ?? 0) * 60; widget.onChanged(); },
              ),
            )
          else
            Expanded(
              child: TextField(
                controller: _litersController,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(labelText: 'Liters', isDense: true, border: OutlineInputBorder()),
                onChanged: (v) { s.runTargetLiters = int.tryParse(v) ?? 0; widget.onChanged(); },
              ),
            ),
        ]),
        const SizedBox(height: 12),
        Row(children: [
          const Text('Fertigation dosing', style: TextStyle(fontWeight: FontWeight.w500, fontSize: 13)),
          const Spacer(),
          Switch(value: s.doseEnabled, onChanged: (v) { setState(() { s.doseEnabled = v; }); widget.onChanged(); }),
        ]),
        if (s.doseEnabled)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Row(children: [
              Expanded(
                child: DropdownButtonFormField<DoseTiming>(
                  value: s.doseTiming,
                  decoration: const InputDecoration(labelText: 'Timing', isDense: true, border: OutlineInputBorder()),
                  items: const [
                    DropdownMenuItem(value: DoseTiming.start, child: Text('Start')),
                    DropdownMenuItem(value: DoseTiming.mid, child: Text('Mid')),
                    DropdownMenuItem(value: DoseTiming.end, child: Text('End')),
                  ],
                  onChanged: (v) { setState(() { s.doseTiming = v!; }); widget.onChanged(); },
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: TextField(
                  controller: _doseDurationController,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(labelText: 'Dose minutes', isDense: true, border: OutlineInputBorder()),
                  onChanged: (v) { s.doseDurationSec = (int.tryParse(v) ?? 0) * 60; widget.onChanged(); },
                ),
              ),
            ]),
          ),
      ]),
    );
  }
}

class _Card extends StatelessWidget {
  final List<Widget> children;
  const _Card({required this.children});
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          color: Theme.of(context).cardColor,
          borderRadius: BorderRadius.circular(12),
          boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05), blurRadius: 8, offset: const Offset(0, 2))],
        ),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: children),
      );
}

class _Label extends StatelessWidget {
  final String text;
  const _Label(this.text);
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(text, style: const TextStyle(fontWeight: FontWeight.w500, fontSize: 14)),
      );
}
