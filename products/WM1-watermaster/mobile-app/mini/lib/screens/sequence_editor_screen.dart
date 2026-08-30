import 'package:flutter/material.dart';
import '../models/program.dart';
import '../models/device_status.dart';

/// Standalone full-screen editor for a single Sequence — used by the
/// Sequence Library screen (add/edit a reusable template) and reachable
/// from a program's "Save to Library" action. Program editing itself
/// still uses its own inline card editor (program_editor_screen.dart)
/// so multiple sequences in one program can be tweaked without
/// navigating away — this screen is specifically for standalone,
/// library-level editing.
class SequenceEditorScreen extends StatefulWidget {
  final Sequence? sequence;
  final RelayNames relayNames;
  const SequenceEditorScreen({super.key, this.sequence, required this.relayNames});

  @override
  State<SequenceEditorScreen> createState() => _SequenceEditorScreenState();
}

class _SequenceEditorScreenState extends State<SequenceEditorScreen> {
  late final TextEditingController _nameController;
  late final TextEditingController _durationController;
  late final TextEditingController _litersController;
  late final TextEditingController _doseDurationController;
  late Sequence _seq;

  @override
  void initState() {
    super.initState();
    _seq = widget.sequence?.copy() ?? Sequence(name: 'New Sequence');
    _nameController = TextEditingController(text: _seq.name);
    _durationController = TextEditingController(text: (_seq.runTargetSec ~/ 60).toString());
    _litersController = TextEditingController(text: _seq.runTargetLiters.toString());
    _doseDurationController = TextEditingController(text: (_seq.doseDurationSec ~/ 60).toString());
  }

  @override
  void dispose() {
    _nameController.dispose();
    _durationController.dispose();
    _litersController.dispose();
    _doseDurationController.dispose();
    super.dispose();
  }

  void _save() {
    if (_nameController.text.trim().isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Give the sequence a name')));
      return;
    }
    if (_seq.valveMask == 0) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(content: Text('Select at least one valve')));
      return;
    }
    _seq.name = _nameController.text.trim();
    Navigator.pop(context, _seq);
  }

  @override
  Widget build(BuildContext context) {
    final names = widget.relayNames;
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.sequence != null ? 'Edit Sequence' : 'New Sequence', style: const TextStyle(fontWeight: FontWeight.w600)),
        centerTitle: true,
        actions: [TextButton(onPressed: _save, child: const Text('Save', style: TextStyle(fontWeight: FontWeight.w600)))],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _Card(children: [
            _Label('Sequence Name'),
            TextField(controller: _nameController, decoration: _dec('e.g. Drip Zone A')),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            _Label('Valves'),
            Wrap(spacing: 8, children: [
              for (int v = 0; v < 4; v++)
                FilterChip(
                  label: Text(names.valveName(v)),
                  selected: _seq.valveOn(v),
                  onSelected: (sel) => setState(() => _seq.setValve(v, sel)),
                  selectedColor: const Color(0xFF2196F3).withOpacity(0.2),
                  checkmarkColor: const Color(0xFF1565C0),
                ),
            ]),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            _Label('Run Mode'),
            Row(children: [
              Expanded(
                child: DropdownButtonFormField<RunMode>(
                  value: _seq.runMode,
                  decoration: const InputDecoration(isDense: true, border: OutlineInputBorder()),
                  items: const [
                    DropdownMenuItem(value: RunMode.time, child: Text('Time-based')),
                    DropdownMenuItem(value: RunMode.volume, child: Text('Volume-based')),
                  ],
                  onChanged: (v) => setState(() => _seq.runMode = v!),
                ),
              ),
              const SizedBox(width: 8),
              if (_seq.runMode == RunMode.time)
                Expanded(
                  child: TextField(
                    controller: _durationController,
                    keyboardType: TextInputType.number,
                    decoration: const InputDecoration(labelText: 'Minutes', isDense: true, border: OutlineInputBorder()),
                    onChanged: (v) => _seq.runTargetSec = (int.tryParse(v) ?? 0) * 60,
                  ),
                )
              else
                Expanded(
                  child: TextField(
                    controller: _litersController,
                    keyboardType: TextInputType.number,
                    decoration: const InputDecoration(labelText: 'Liters', isDense: true, border: OutlineInputBorder()),
                    onChanged: (v) => _seq.runTargetLiters = int.tryParse(v) ?? 0,
                  ),
                ),
            ]),
          ]),
          const SizedBox(height: 12),
          _Card(children: [
            Row(children: [
              const Text('Fertigation dosing', style: TextStyle(fontWeight: FontWeight.w500)),
              const Spacer(),
              Switch(value: _seq.doseEnabled, onChanged: (v) => setState(() => _seq.doseEnabled = v)),
            ]),
            if (_seq.doseEnabled)
              Padding(
                padding: const EdgeInsets.only(top: 8),
                child: Row(children: [
                  Expanded(
                    child: DropdownButtonFormField<DoseTiming>(
                      value: _seq.doseTiming,
                      decoration: const InputDecoration(labelText: 'Timing', isDense: true, border: OutlineInputBorder()),
                      items: const [
                        DropdownMenuItem(value: DoseTiming.start, child: Text('Start')),
                        DropdownMenuItem(value: DoseTiming.mid, child: Text('Mid')),
                        DropdownMenuItem(value: DoseTiming.end, child: Text('End')),
                      ],
                      onChanged: (v) => setState(() => _seq.doseTiming = v!),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: TextField(
                      controller: _doseDurationController,
                      keyboardType: TextInputType.number,
                      decoration: const InputDecoration(labelText: 'Dose minutes', isDense: true, border: OutlineInputBorder()),
                      onChanged: (v) => _seq.doseDurationSec = (int.tryParse(v) ?? 0) * 60,
                    ),
                  ),
                ]),
              ),
          ]),
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
