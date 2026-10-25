import 'package:flutter/material.dart';

import '../services/node_store.dart';
import 'node_screen.dart';

class SettingsTab extends StatefulWidget {
  const SettingsTab({super.key, required this.ctl});
  final NodeController ctl;
  @override
  State<SettingsTab> createState() => _SettingsTabState();
}

class _SettingsTabState extends State<SettingsTab> {
  Map<String, dynamic>? c;
  Map<String, dynamic>? dev;
  final _f = <String, TextEditingController>{};

  TextEditingController f(String k, [Object? init]) =>
      _f.putIfAbsent(k, () => TextEditingController(text: init == null ? '' : '$init'));

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final r = await widget.ctl.send({'cmd': 'get_config'});
    final d = await widget.ctl.send({'cmd': 'device_info'});
    if (!mounted) return;
    setState(() {
      if (r['ok'] == true) {
        c = Map<String, dynamic>.from(r['data']);
        _f.clear();
      }
      if (d['ok'] == true) dev = Map<String, dynamic>.from(d['data']);
    });
  }

  Future<void> _send(Map<String, dynamic> cmd, String ok) async {
    final r = await widget.ctl.send(cmd);
    if (mounted) showReply(context, r, ok);
  }

  int _i(String k) => int.tryParse(f(k).text) ?? 0;
  double _d(String k) => double.tryParse(f(k).text) ?? 0;

  @override
  Widget build(BuildContext context) {
    final cfg = c;
    if (cfg == null) return const Center(child: CircularProgressIndicator());
    final valves = (cfg['valves'] as List).map((e) => Map<String, dynamic>.from(e)).toList();
    final flow = (cfg['flow'] as List).map((e) => Map<String, dynamic>.from(e)).toList();
    final t = Theme.of(context).textTheme;
    Widget field(String k, String label, Object? init, {bool num = false, bool secret = false, double w = 150}) => SizedBox(
          width: w,
          child: TextField(
            controller: f(k, init),
            obscureText: secret,
            keyboardType: num ? TextInputType.number : null,
            decoration: InputDecoration(labelText: label),
          ),
        );
    Widget section(String title, List<Widget> children, VoidCallback onSave) => Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(title, style: t.titleMedium),
              Wrap(spacing: 12, runSpacing: 4, children: children),
              const SizedBox(height: 8),
              Align(alignment: Alignment.centerRight, child: FilledButton(onPressed: onSave, child: const Text('Save'))),
            ]),
          ),
        );

    return RefreshIndicator(
      onRefresh: _load,
      child: ListView(padding: const EdgeInsets.all(12), children: [
        section('Farm WiFi', [
          field('ssid', 'SSID', cfg['ssid'], w: 200),
          field('wpass', 'Password', '', secret: true, w: 200),
          TextButton.icon(
            icon: const Icon(Icons.wifi_find),
            label: const Text('Scan'),
            onPressed: () async {
              final r = await widget.ctl.send({'cmd': 'wifi_scan'}, timeout: const Duration(seconds: 15));
              final nets = ((r['data']?['networks'] as List?) ?? []).map((e) => Map<String, dynamic>.from(e)).toList();
              if (!context.mounted || nets.isEmpty) return;
              final pick = await showDialog<String>(
                context: context,
                builder: (ctx) => SimpleDialog(title: const Text('Networks'), children: [
                  for (final n in nets)
                    SimpleDialogOption(onPressed: () => Navigator.pop(ctx, n['ssid']), child: Text('${n['ssid']}  (${n['rssi']} dBm)')),
                ]),
              );
              if (pick != null) setState(() => f('ssid').text = pick);
            },
          ),
        ], () => _send({'cmd': 'wifi_config', 'ssid': f('ssid').text.trim(), 'pass': f('wpass').text}, 'WiFi saved; node is joining')),
        section('MQTT broker (node)', [
          SizedBox(
            width: 220,
            child: DropdownButtonFormField<String>(
              value: cfg['broker'] == 'girish' ? 'girish' : 'agrisense',
              decoration: const InputDecoration(labelText: 'Node reports to'),
              items: const [
                DropdownMenuItem(value: 'agrisense', child: Text('AgriSense')),
                DropdownMenuItem(value: 'girish', child: Text('Girish (own broker)')),
              ],
              onChanged: (v) => setState(() => cfg['broker'] = v),
            ),
          ),
          if (dev != null)
            Padding(
              padding: const EdgeInsets.only(top: 18),
              child: Text('now: ${dev!['broker']} ${dev!['mqtt'] == true ? 'connected' : 'not connected'}'),
            ),
          if (cfg['broker'] != 'girish') ...[
            field('ahost', 'AgriSense host', cfg['ag_host'], w: 220),
            field('aport', 'Port', cfg['ag_port'], num: true, w: 90),
            field('auser', 'User', cfg['ag_user']),
            field('apass', 'Password (blank = unchanged)', '', secret: true, w: 200),
            field('aid', 'Device ID', cfg['ag_id'], w: 140),
          ] else ...[
            field('mhost', 'Girish host', cfg['mqtt_host'], w: 220),
            field('mport', 'Port', cfg['mqtt_port'], num: true, w: 90),
            field('muser', 'User', cfg['mqtt_user']),
            field('mpass', 'Password (blank = unchanged)', '', secret: true, w: 200),
            field('farm', 'Farm', cfg['farm'], w: 100),
            field('zone', 'Zone', cfg['zone'], w: 80),
            field('node', 'Node', cfg['node'], w: 80),
          ],
        ], () {
          final ag = cfg['broker'] != 'girish';
          final pw = ag ? f('apass').text : f('mpass').text;
          _send(
              ag
                  ? {
                      'cmd': 'mqtt_config',
                      'broker': 'agrisense',
                      'host': f('ahost').text.trim(),
                      'port': _i('aport'),
                      'user': f('auser').text.trim(),
                      'ag_id': f('aid').text.trim(),
                      if (pw.isNotEmpty) 'pass': pw,
                    }
                  : {
                      'cmd': 'mqtt_config',
                      'broker': 'girish',
                      'host': f('mhost').text.trim(),
                      'port': _i('mport'),
                      'user': f('muser').text.trim(),
                      'farm': f('farm').text.trim(),
                      'zone': f('zone').text.trim(),
                      'node': f('node').text.trim(),
                      if (pw.isNotEmpty) 'pass': pw,
                    },
              'Saved: node now uses the ${ag ? 'AgriSense' : 'Girish'} broker');
          widget.ctl.entry.broker = ag ? 'agrisense' : 'girish';
          widget.ctl.store.save();
        }),
        section('Reporting', [
          field('act', 'Active interval s (30-1800)', cfg['active_s'], num: true, w: 200),
          field('hb', 'Heartbeat s (60-14400)', cfg['heartbeat_s'], num: true, w: 200),
          field('idl', 'Idle delay s', cfg['idle_delay_s'], num: true),
        ], () => _send({'cmd': 'set_report', 'active_s': _i('act'), 'heartbeat_s': _i('hb'), 'idle_delay_s': _i('idl')}, 'Reporting saved')),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Valves', style: t.titleMedium),
              for (final v in valves) _ValveHw(key: ValueKey('vhw${v['valve']}'), v: v, ctl: widget.ctl),
            ]),
          ),
        ),
        section('Flow sensors', [
          for (final fl in flow) ...[
            field('fv${fl['input']}', 'Flow ${fl['input']} valve (0=unused)', fl['valve'], num: true, w: 170),
            field('fp${fl['input']}', 'pulses per litre', fl['ppl'], num: true, w: 130),
          ],
        ], () => _send({
              'cmd': 'set_config',
              'config': {
                'flow': [
                  for (final fl in flow)
                    {'input': fl['input'], 'valve': _i('fv${fl['input']}'), 'ppl': _d('fp${fl['input']}')}
                ]
              }
            }, 'Flow sensors saved')),
        section('Options', [
          SizedBox(
            width: 220,
            child: DropdownButtonFormField<bool>(
              value: cfg['sequential'] == true,
              decoration: const InputDecoration(labelText: 'V1 and V2'),
              items: const [
                DropdownMenuItem(value: true, child: Text('One after the other')),
                DropdownMenuItem(value: false, child: Text('Together')),
              ],
              onChanged: (v) => cfg['sequential'] = v,
            ),
          ),
          field('settle', 'Pressure settle s', cfg['ps_settle_s'], num: true),
          field('cap', 'Battery Ah', cfg['capacity_ah'], num: true),
          field('tz', 'Time zone (min from UTC)', cfg['tz_min'], num: true, w: 190),
        ], () async {
          await _send({
            'cmd': 'set_options',
            'sequential': cfg['sequential'] == true,
            'ps_settle_s': _d('settle'),
            'tz_min': _i('tz'),
          }, 'Options saved');
          await widget.ctl.send({'cmd': 'set_battery', 'capacity_ah': _d('cap')});
        }),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Device', style: t.titleMedium),
              if (dev != null)
                Text('fw ${dev!['fw']}  |  ${dev!['chip']}  |  ${dev!['mac']}\n'
                    '${dev!['host']}  |  ${dev!['ip'] ?? ''}  |  SoftAP ${dev!['ap'] == true ? dev!['ap_ssid'] : 'off'}\n'
                    'storage ${((dev!['fs_used'] ?? 0) / 1024).round()} / ${((dev!['fs_total'] ?? 0) / 1024).round()} KB\n${dev!['broker']} broker ${dev!['broker_host'] ?? ''}, topics ${dev!['base']}'),
              const SizedBox(height: 8),
              Wrap(spacing: 8, runSpacing: 8, children: [
                OutlinedButton(
                  onPressed: () => _send({'cmd': 'rtc_sync', 'epoch': DateTime.now().millisecondsSinceEpoch ~/ 1000}, 'Clock set from phone'),
                  child: const Text('Set clock from phone'),
                ),
                OutlinedButton(onPressed: () => _send({'cmd': 'softap_on', 'minutes': 15}, 'SoftAP on for 15 min'), child: const Text('SoftAP on')),
                OutlinedButton(onPressed: () => _send({'cmd': 'reboot'}, 'Rebooting'), child: const Text('Reboot')),
              ]),
            ]),
          ),
        ),
      ]),
    );
  }
}

class _ValveHw extends StatefulWidget {
  const _ValveHw({super.key, required this.v, required this.ctl});
  final Map<String, dynamic> v;
  final NodeController ctl;
  @override
  State<_ValveHw> createState() => _ValveHwState();
}

class _ValveHwState extends State<_ValveHw> {
  late final pulse = TextEditingController(text: '${widget.v['pulse_ms']}');
  late final after = TextEditingController(text: '${widget.v['close_after_min']}');
  late bool invert = widget.v['invert'] == true;
  late bool never = widget.v['never_close'] == true;
  late String loss = widget.v['on_water_loss'] ?? 'keep';

  @override
  Widget build(BuildContext context) {
    final n = widget.v['valve'];
    return ExpansionTile(
      tilePadding: EdgeInsets.zero,
      title: Text('Valve $n: pulse ${pulse.text} ms, water lost: $loss'),
      children: [
        Wrap(spacing: 12, children: [
          SizedBox(width: 120, child: TextField(controller: pulse, keyboardType: TextInputType.number, decoration: const InputDecoration(labelText: 'Pulse ms'))),
          SizedBox(
            width: 200,
            child: DropdownButtonFormField<String>(
              value: loss,
              decoration: const InputDecoration(labelText: 'When water is lost'),
              items: const [
                DropdownMenuItem(value: 'keep', child: Text('Keep open')),
                DropdownMenuItem(value: 'close', child: Text('Close after')),
              ],
              onChanged: (v) => setState(() => loss = v!),
            ),
          ),
          SizedBox(width: 120, child: TextField(controller: after, keyboardType: TextInputType.number, decoration: const InputDecoration(labelText: 'Close after min'))),
        ]),
        SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Invert polarity'), value: invert, onChanged: (v) => setState(() => invert = v)),
        SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Never close (relief valve until pump relief is fitted)'),
            value: never,
            onChanged: (v) => setState(() => never = v)),
        Row(mainAxisAlignment: MainAxisAlignment.end, children: [
          TextButton(
            onPressed: () async {
              final r = await widget.ctl.send({'cmd': 'valve_test', 'valve': n}, timeout: const Duration(seconds: 15));
              if (context.mounted) showReply(context, r, 'V$n pulsed open then closed');
            },
            child: const Text('Test pulse'),
          ),
          FilledButton(
            onPressed: () async {
              final r = await widget.ctl.send({
                'cmd': 'set_valve_hw',
                'valve': n,
                'pulse_ms': int.tryParse(pulse.text) ?? 50,
                'invert': invert,
                'never_close': never,
                'on_water_loss': loss,
                'close_after_min': int.tryParse(after.text) ?? 10,
              });
              if (context.mounted) showReply(context, r, 'Valve $n saved');
              setState(() {});
            },
            child: const Text('Save'),
          ),
        ]),
      ],
    );
  }
}
