import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';

class StatusTab extends StatelessWidget {
  const StatusTab({super.key, required this.conn});
  final HouseConn conn;

  @override
  Widget build(BuildContext context) {
    final s = St(conn.status!);
    final cs = Theme.of(context).colorScheme;
    final set = s.settings;
    String phase = '';
    if (s.phaseLeft > 0) {
      phase = s.reason == 'misting_on'
          ? 'ON for ${mmss(s.phaseLeft)} more'
          : s.reason == 'misting_gap'
              ? 'Next pulse in ${mmss(s.phaseLeft)}'
              : s.reason == 'manual_on'
                  ? 'Auto-stop in ${mmss(s.phaseLeft)}'
                  : '';
    }

    return ListView(padding: const EdgeInsets.only(bottom: 24), children: [
      // ---- Pump ----
      Card(
        margin: const EdgeInsets.fromLTRB(12, 12, 12, 6),
        child: Padding(
          padding: const EdgeInsets.all(18),
          child: Column(children: [
            Row(children: [
              PumpBadge(on: s.pumpOn, size: 72),
              const SizedBox(width: 18),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Text(s.pumpOn ? 'PUMP ON' : 'PUMP OFF',
                      style: TextStyle(
                          fontSize: 24,
                          fontWeight: FontWeight.w700,
                          color: s.pumpOn ? const Color(0xFF1E88E5) : cs.onSurface)),
                  const SizedBox(height: 4),
                  Text(reasonText(s.reason), style: TextStyle(color: cs.onSurfaceVariant)),
                  if (phase.isNotEmpty)
                    Padding(
                      padding: const EdgeInsets.only(top: 4),
                      child: Text(phase, style: const TextStyle(fontWeight: FontWeight.w600)),
                    ),
                ]),
              ),
            ]),
            const SizedBox(height: 16),
            SizedBox(
              width: double.infinity,
              child: SegmentedButton<String>(
                segments: const [
                  ButtonSegment(value: 'auto', label: Text('Auto'), icon: Icon(Icons.auto_mode)),
                  ButtonSegment(value: 'manual', label: Text('Manual'), icon: Icon(Icons.pan_tool_alt_outlined)),
                  ButtonSegment(value: 'off', label: Text('Off'), icon: Icon(Icons.power_settings_new)),
                ],
                selected: {s.mode},
                onSelectionChanged: (v) => _setMode(context, v.first),
              ),
            ),
            if (s.mode == 'manual') ...[
              const SizedBox(height: 12),
              Row(children: [
                Expanded(
                  child: FilledButton.icon(
                    style: FilledButton.styleFrom(backgroundColor: const Color(0xFF1E88E5)),
                    onPressed: s.pumpOn ? null : () => runCmd(context, conn.send({'cmd': 'pump_on'})),
                    icon: const Icon(Icons.play_arrow),
                    label: const Text('Pump ON'),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: s.pumpOn ? () => runCmd(context, conn.send({'cmd': 'pump_off'})) : null,
                    icon: const Icon(Icons.stop),
                    label: const Text('Pump OFF'),
                  ),
                ),
              ]),
              const SizedBox(height: 6),
              Text('Manual ON stops by itself after ${set['manual_max'] ?? 30} min and returns to Auto.',
                  style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
            ],
          ]),
        ),
      ),

      // ---- Climate ----
      Row(children: [
        Expanded(
          child: _ClimateCard(
            title: 'Inside (control)',
            t: s.controlT,
            rh: s.controlRh,
            note: !s.controlOk
                ? 'No reading'
                : s.controlFallback
                    ? 'Using outside sensor (no sensor chosen)'
                    : s.controlUsed == 1
                        ? '1 sensor'
                        : 'Average of ${s.controlUsed}${s.controlUsed < s.controlSelected ? ' of ${s.controlSelected}' : ''}',
            highlight: true,
          ),
        ),
        Expanded(
          child: _ClimateCard(
            title: 'Outside (pump panel)',
            t: s.outsideT,
            rh: s.outsideRh,
            note: s.outsideOk ? s.outsideSensor : 'Sensor not found',
          ),
        ),
      ]),

      // ---- Rules summary ----
      Card(
        margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            _rule(Icons.play_circle_outline, Colors.green,
                'Start at ${set['t_start']} °C, or RH ≤ ${set['rh_low']} % when above ${set['t_stop']} °C'),
            _rule(Icons.stop_circle_outlined, Colors.red,
                'Stop at ${set['t_stop']} °C or RH ≥ ${set['rh_high']} %'
                '${set['t_hyst'] == null ? '' : ' · restart from ${((set['t_stop'] as num) + (set['t_hyst'] as num)).toStringAsFixed(1)} °C / ${((set['rh_high'] as num) - (set['rh_hyst'] as num)).toStringAsFixed(0)} %'}'),
            _rule(Icons.timelapse, cs.primary,
                'Pulse ${mmss(set['on_s'] ?? 0)} ON / ${mmss(set['off_s'] ?? 0)} OFF'),
            _rule(Icons.schedule, s.inWindow ? cs.primary : Colors.orange,
                set['win_on'] == true && set['win_start'] != set['win_end']
                    ? 'Working hours ${hhmmMin(set['win_start'] ?? 0)} – ${hhmmMin(set['win_end'] ?? 0)}${s.inWindow ? '' : '  (now outside)'}'
                    : 'Working hours: all day'),
            if (!s.clockOk)
              _rule(Icons.warning_amber, Colors.orange, 'Controller clock not set — working hours ignored'),
          ]),
        ),
      ),

      // ---- Today ----
      Card(
        margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 14),
          child: Row(mainAxisAlignment: MainAxisAlignment.spaceAround, children: [
            _stat('${s.startsToday}', 'starts today'),
            _stat('${s.onMinToday} min', 'pump ON today'),
            _stat('${s.startsHr}/${set['max_starts'] ?? '-'}', 'starts last hour'),
          ]),
        ),
      ),

      // ---- Sensors quick view ----
      if (s.nodes.isNotEmpty) ...[
        const SectionTitle('Sensors'),
        for (final n in s.nodes)
          ListTile(
            dense: true,
            leading: Icon(n['online'] == true ? Icons.sensors : Icons.sensors_off,
                color: n['online'] == true ? cs.primary : Colors.grey),
            title: Text((n['name'] as String?)?.isNotEmpty == true ? n['name'] : n['id']),
            subtitle: n['control'] == true ? const Text('in control') : null,
            trailing: Text(
                n['online'] == true ? '${fmtT((n['t'] as num?)?.toDouble())}   ${fmtRh((n['rh'] as num?)?.toDouble())}' : 'offline',
                style: const TextStyle(fontSize: 15)),
          ),
      ],
    ]);
  }

  Future<void> _setMode(BuildContext context, String m) async {
    if (m == 'off') {
      final ok = await showDialog<bool>(
        context: context,
        builder: (c) => AlertDialog(
          title: const Text('Switch misting OFF?'),
          content: const Text('The pump will not run until you choose Auto or Manual again.'),
          actions: [
            TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
            FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('Switch off')),
          ],
        ),
      );
      if (ok != true) return;
    }
    if (!context.mounted) return;
    runCmd(context, conn.send({'cmd': 'set_mode', 'mode': m}));
  }

  Widget _rule(IconData i, Color c, String t) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 3),
        child: Row(children: [Icon(i, size: 18, color: c), const SizedBox(width: 10), Expanded(child: Text(t))]),
      );

  Widget _stat(String v, String l) => Column(children: [
        Text(v, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
        Text(l, style: const TextStyle(fontSize: 12)),
      ]);
}

class _ClimateCard extends StatelessWidget {
  const _ClimateCard({required this.title, this.t, this.rh, required this.note, this.highlight = false});
  final String title;
  final double? t, rh;
  final String note;
  final bool highlight;
  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Card(
      margin: const EdgeInsets.fromLTRB(12, 6, 0, 6).copyWith(right: highlight ? 6 : 12, left: highlight ? 12 : 6),
      color: highlight ? cs.primaryContainer : null,
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(title, style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
          const SizedBox(height: 6),
          Text(fmtT(t), style: const TextStyle(fontSize: 26, fontWeight: FontWeight.w700)),
          Text('RH ${fmtRh(rh)}', style: const TextStyle(fontSize: 18)),
          const SizedBox(height: 4),
          Text(note, style: TextStyle(fontSize: 11, color: cs.onSurfaceVariant), maxLines: 2),
        ]),
      ),
    );
  }
}
