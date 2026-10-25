import 'package:flutter/material.dart';

import '../services/node_store.dart';
import 'node_screen.dart';

const _days = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];

class ProgramsTab extends StatefulWidget {
  const ProgramsTab({super.key, required this.ctl});
  final NodeController ctl;
  @override
  State<ProgramsTab> createState() => _ProgramsTabState();
}

class _ProgramsTabState extends State<ProgramsTab> {
  List<_Prog>? progs;
  String? error;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final r = await widget.ctl.send({'cmd': 'get_program'});
    if (!mounted) return;
    setState(() {
      if (r['ok'] == true) {
        progs = (r['data']['programs'] as List).map((e) => _Prog.fromJson(Map<String, dynamic>.from(e))).toList();
        error = null;
      } else {
        error = r['err']?.toString();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    if (progs == null) {
      return Center(
          child: error == null
              ? const CircularProgressIndicator()
              : Column(mainAxisSize: MainAxisSize.min, children: [Text(error!), TextButton(onPressed: _load, child: const Text('Retry'))]));
    }
    return RefreshIndicator(
      onRefresh: _load,
      child: ListView(padding: const EdgeInsets.all(12), children: [
        const Padding(
          padding: EdgeInsets.only(bottom: 8),
          child: Text('On programmed days a valve runs whenever water is present until its minutes (or litres) are done. '
              'Time without water does not count, so a power cut only pauses it.'),
        ),
        for (final p in progs!) _card(p),
      ]),
    );
  }

  Widget _card(_Prog p) {
    final t = Theme.of(context).textTheme;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Text('Valve ${p.valve}', style: t.titleMedium),
            const Spacer(),
            DropdownButton<String>(
              value: p.mode,
              items: const [
                DropdownMenuItem(value: 'program', child: Text('Program')),
                DropdownMenuItem(value: 'manual', child: Text('Manual only')),
                DropdownMenuItem(value: 'off', child: Text('Off')),
              ],
              onChanged: (v) => setState(() => p.mode = v!),
            ),
          ]),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Program enabled'),
            value: p.enabled,
            onChanged: (v) => setState(() => p.enabled = v),
          ),
          Wrap(spacing: 6, children: [
            for (final d in _days)
              FilterChip(
                label: Text(d),
                selected: p.days.contains(d),
                onSelected: (sel) => setState(() => sel ? p.days.add(d) : p.days.remove(d)),
              ),
          ]),
          const SizedBox(height: 8),
          Wrap(crossAxisAlignment: WrapCrossAlignment.center, spacing: 12, runSpacing: 8, children: [
            SegmentedButton<String>(
              segments: const [
                ButtonSegment(value: 'timer', label: Text('Minutes')),
                ButtonSegment(value: 'litres', label: Text('Litres')),
              ],
              selected: {p.type},
              onSelectionChanged: (s) => setState(() => p.type = s.first),
            ),
            SizedBox(
              width: 110,
              child: TextFormField(
                key: ValueKey('amt${p.valve}${p.type}'),
                initialValue: '${p.type == 'litres' ? p.litres : p.minutes}',
                keyboardType: TextInputType.number,
                decoration: InputDecoration(suffixText: p.type == 'litres' ? 'L/day' : 'min/day'),
                onChanged: (v) {
                  final n = int.tryParse(v) ?? 0;
                  if (p.type == 'litres') {
                    p.litres = n;
                  } else {
                    p.minutes = n;
                  }
                },
              ),
            ),
          ]),
          const SizedBox(height: 8),
          Row(children: [
            const Text('Window '),
            TextButton(onPressed: () => _pick(p, 0), child: Text(p.window[0])),
            const Text('to'),
            TextButton(onPressed: () => _pick(p, 1), child: Text(p.window[1])),
          ]),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Carry unfinished time to the next day'),
            value: p.carry,
            onChanged: (v) => setState(() => p.carry = v),
          ),
          Align(
            alignment: Alignment.centerRight,
            child: FilledButton(onPressed: () => _save(p), child: const Text('Save')),
          ),
        ]),
      ),
    );
  }

  Future<void> _pick(_Prog p, int i) async {
    final parts = p.window[i].split(':');
    final t = await showTimePicker(
        context: context, initialTime: TimeOfDay(hour: int.parse(parts[0]), minute: int.parse(parts[1])));
    if (t == null) return;
    setState(() => p.window[i] = '${t.hour.toString().padLeft(2, '0')}:${t.minute.toString().padLeft(2, '0')}');
  }

  Future<void> _save(_Prog p) async {
    if (p.enabled && p.days.isEmpty) {
      showReply(context, {'ok': false, 'err': 'pick at least one day'}, '');
      return;
    }
    final prog = {
      'enabled': p.enabled,
      'type': p.type,
      'days': _days.where(p.days.contains).toList(),
      if (p.type == 'litres') 'litres': p.litres else 'minutes': p.minutes,
      'window': p.window,
      'carry_over': p.carry,
    };
    var r = await widget.ctl.send({'cmd': 'set_program', 'valve': p.valve, 'program': prog, 'set_mode': false});
    if (r['ok'] == true) r = await widget.ctl.send({'cmd': 'set_mode', 'valve': p.valve, 'mode': p.mode});
    if (mounted) showReply(context, r, 'Valve ${p.valve} program saved');
  }
}

class _Prog {
  int valve;
  String mode;
  bool enabled;
  String type;
  Set<String> days;
  int minutes, litres;
  List<String> window;
  bool carry;
  _Prog(this.valve, this.mode, this.enabled, this.type, this.days, this.minutes, this.litres, this.window, this.carry);

  factory _Prog.fromJson(Map<String, dynamic> j) {
    final p = Map<String, dynamic>.from(j['program'] ?? {});
    final w = (p['window'] as List?)?.map((e) => '$e').toList() ?? ['00:00', '23:59'];
    return _Prog(
      j['valve'] ?? 1,
      j['mode'] ?? 'program',
      p['enabled'] ?? false,
      p['type'] ?? 'timer',
      ((p['days'] as List?) ?? []).map((e) => '$e').toSet(),
      p['minutes'] ?? 30,
      p['litres'] ?? 100,
      w.length == 2 ? w : ['00:00', '23:59'],
      p['carry_over'] ?? false,
    );
  }
}
