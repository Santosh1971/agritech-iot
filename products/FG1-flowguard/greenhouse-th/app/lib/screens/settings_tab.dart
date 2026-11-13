import 'dart:async';

import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';

class SettingsTab extends StatefulWidget {
  const SettingsTab({super.key, required this.store, required this.conn});
  final HouseStore store;
  final HouseConn conn;
  @override
  State<SettingsTab> createState() => _SettingsTabState();
}

class _SettingsTabState extends State<SettingsTab> {
  Map<String, dynamic>? _edit; // local copy while editing
  bool _dirty = false;
  bool _saving = false;

  Map<String, dynamic> get _dev => St(widget.conn.status!).settings;

  @override
  Widget build(BuildContext context) {
    if (!_dirty) _edit = Map<String, dynamic>.from(_dev);
    final e = _edit!;
    final s = St(widget.conn.status!);
    final cs = Theme.of(context).colorScheme;

    num n(String k) => (e[k] as num?) ?? 0;
    void set(String k, num v) => setState(() {
          e[k] = v;
          _dirty = true;
        });

    return ListView(padding: const EdgeInsets.only(bottom: 40), children: [
      const SectionTitle('Start misting'),
      _Stepper(label: 'Temperature reaches', value: n('t_start'), unit: '°C', step: 0.5, min: 20, max: 50,
          onChanged: (v) => set('t_start', v)),
      _Stepper(label: 'or humidity falls to (and temp above stop)', value: n('rh_low'), unit: '%', step: 1, min: 10, max: 90,
          onChanged: (v) => set('rh_low', v)),
      const SectionTitle('Stop misting (stop always wins)'),
      _Stepper(label: 'Temperature falls to', value: n('t_stop'), unit: '°C', step: 0.5, min: 15, max: 45,
          onChanged: (v) => set('t_stop', v)),
      _Stepper(label: 'or humidity reaches', value: n('rh_high'), unit: '%', step: 1, min: 20, max: 100,
          onChanged: (v) => set('rh_high', v)),
      // Controllers from fw 0.1.3 report the margins; older ones don't have them.
      if (e.containsKey('t_hyst')) ...[
        const SectionTitle('Restart margin (after a stop)'),
        _Stepper(
            label: 'Temperature: restart only at ${(n('t_stop') + n('t_hyst')).toStringAsFixed(1)} °C or above',
            value: n('t_hyst'), unit: '°C', step: 0.5, min: 0, max: 5,
            onChanged: (v) => set('t_hyst', v)),
        _Stepper(
            label: 'Humidity: restart only at ${(n('rh_high') - n('rh_hyst')).toStringAsFixed(0)} % or below',
            value: n('rh_hyst'), unit: '%', step: 1, min: 0, max: 20,
            onChanged: (v) => set('rh_hyst', v)),
      ],
      const SectionTitle('Pulse'),
      _Stepper(label: 'Pump ON for', value: n('on_s'), unit: 's', step: 10, min: 10, max: 1800, asTime: true,
          onChanged: (v) => set('on_s', v)),
      _Stepper(label: 'Then OFF for', value: n('off_s'), unit: 's', step: 10, min: 30, max: 3600, asTime: true,
          onChanged: (v) => set('off_s', v)),
      _Stepper(label: 'Max pump starts per hour (motor protection)', value: n('max_starts'), unit: '', step: 1, min: 0, max: 30,
          onChanged: (v) => set('max_starts', v)),
      const SectionTitle('Working hours'),
      SwitchListTile(
        title: const Text('Mist only between'),
        value: e['win_on'] == true,
        onChanged: (v) => setState(() {
          e['win_on'] = v;
          _dirty = true;
        }),
      ),
      if (e['win_on'] == true)
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(children: [
            Expanded(child: _timeButton(context, 'From', n('win_start').toInt(), (m) => set('win_start', m))),
            const SizedBox(width: 12),
            Expanded(child: _timeButton(context, 'To', n('win_end').toInt(), (m) => set('win_end', m))),
          ]),
        ),
      const SectionTitle('Manual'),
      _Stepper(label: 'Manual ON stops after', value: n('manual_max'), unit: 'min', step: 5, min: 5, max: 240,
          onChanged: (v) => set('manual_max', v)),
      const SizedBox(height: 16),
      Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16),
        child: Row(children: [
          if (_dirty)
            TextButton(onPressed: () => setState(() => _dirty = false), child: const Text('Discard')),
          const Spacer(),
          FilledButton.icon(
            onPressed: !_dirty || _saving ? null : _save,
            icon: _saving
                ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.save),
            label: const Text('Save to controller'),
          ),
        ]),
      ),

      const SectionTitle('Controller'),
      ListTile(
        leading: const Icon(Icons.memory),
        title: Text(s.deviceId),
        subtitle: Text('Firmware ${s.fw}  ·  clock ${s.clockOk ? s.time : 'not set'}'),
        trailing: TextButton(
            onPressed: () => runCmd(context, widget.conn.syncClock(), okMsg: 'Clock set from phone'),
            child: const Text('Set clock')),
      ),
      ListTile(
        leading: Icon(s.wifi['connected'] == true ? Icons.wifi : Icons.wifi_off),
        title: Text(s.wifi['connected'] == true ? 'Farm WiFi: ${s.wifi['ssid']}' : 'Not on farm WiFi'),
        subtitle: Text(s.wifi['connected'] == true
            ? 'Signal ${s.wifi['rssi']} dBm · IP ${s.wifi['ip']} · internet ${s.mqtt ? 'OK' : 'not reachable'}'
            : 'Own WiFi ${s.wifi['ap']} is on (password on its label)'),
        trailing: TextButton(onPressed: () => _wifiSetup(context), child: const Text('Set up')),
      ),
      ListTile(
        leading: const Icon(Icons.edit_outlined),
        title: const Text('Greenhouse name and address'),
        subtitle: Text('${widget.conn.house.name} · ${widget.conn.house.host.isEmpty ? 'internet only' : widget.conn.house.host}'),
        onTap: () => _editHouse(context),
      ),
      ListTile(
        leading: const Icon(Icons.restart_alt),
        title: const Text('Restart controller'),
        onTap: () => _confirm(context, 'Restart the controller?', 'The pump stops and misting resumes in Auto after about 3 minutes.',
            () => runCmd(context, widget.conn.send({'cmd': 'reboot'}), okMsg: 'Restarting…')),
      ),
      ListTile(
        leading: Icon(Icons.delete_outline, color: cs.error),
        title: Text('Remove from this app', style: TextStyle(color: cs.error)),
        onTap: () => _confirm(context, 'Remove ${widget.conn.house.name}?', 'Only removes it from this phone. The controller keeps working.',
            () async {
          await widget.store.remove(widget.conn.house);
          if (context.mounted) Navigator.popUntil(context, (r) => r.isFirst);
        }),
      ),
    ]);
  }

  Future<void> _save() async {
    setState(() => _saving = true);
    final e = _edit!;
    final cmd = <String, dynamic>{
      'cmd': 'set_settings',
      for (final k in ['t_start', 't_stop', 'rh_low', 'rh_high', 'on_s', 'off_s', 'win_on', 'win_start', 'win_end', 'max_starts', 'manual_max', 't_hyst', 'rh_hyst'])
        if (e.containsKey(k)) k: e[k],
      'rev': DateTime.now().millisecondsSinceEpoch ~/ 1000,
    };
    final r = await runCmd(context, widget.conn.send(cmd), okMsg: 'Saved');
    if (!mounted) return;
    setState(() {
      _saving = false;
      if (r['ok'] == true) _dirty = false;
    });
  }

  Widget _timeButton(BuildContext context, String label, int minutes, ValueChanged<int> onPick) => OutlinedButton(
        onPressed: () async {
          final t = await showTimePicker(context: context, initialTime: TimeOfDay(hour: minutes ~/ 60, minute: minutes % 60));
          if (t != null) onPick(t.hour * 60 + t.minute);
        },
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 10),
          child: Column(children: [
            Text(label, style: const TextStyle(fontSize: 12)),
            Text(hhmmMin(minutes), style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w600)),
          ]),
        ),
      );

  Future<void> _confirm(BuildContext context, String title, String body, VoidCallback go) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(title: Text(title), content: Text(body), actions: [
        TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
        FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('OK')),
      ]),
    );
    if (ok == true) go();
  }

  Future<void> _editHouse(BuildContext context) async {
    final h = widget.conn.house;
    final name = TextEditingController(text: h.name);
    final host = TextEditingController(text: h.host);
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('Greenhouse'),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          TextField(controller: name, decoration: const InputDecoration(labelText: 'Name')),
          TextField(
              controller: host,
              decoration: const InputDecoration(labelText: 'Controller address', helperText: '192.168.4.1, farm-WiFi IP, or empty')),
        ]),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('Cancel')),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('Save')),
        ],
      ),
    );
    if (ok != true) return;
    final hostChanged = host.text.trim() != h.host;
    h.name = name.text.trim().isEmpty ? h.name : name.text.trim();
    h.host = host.text.trim();
    if (hostChanged) {
      await widget.store.updated(h);
      if (context.mounted) Navigator.popUntil(context, (r) => r.isFirst);
    } else {
      await widget.store.save();
    }
  }

  Future<void> _wifiSetup(BuildContext context) async {
    await showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      builder: (_) => _WifiSheet(conn: widget.conn),
    );
  }
}

class _Stepper extends StatelessWidget {
  const _Stepper(
      {required this.label, required this.value, required this.unit, required this.step, required this.min, required this.max,
      required this.onChanged, this.asTime = false});
  final String label, unit;
  final num value, step, min, max;
  final bool asTime;
  final ValueChanged<num> onChanged;

  String get _text {
    if (asTime) return mmss(value.toInt());
    if (step < 1) return '${value.toStringAsFixed(1)} $unit';
    return value == 0 && unit.isEmpty ? 'no limit' : '${value.toInt()} $unit';
  }

  num _clamp(num v) => v < min ? min : (v > max ? max : v);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
      child: Row(children: [
        Expanded(child: Text(label)),
        IconButton.outlined(
            onPressed: value <= min ? null : () => onChanged(_clamp(value - step)), icon: const Icon(Icons.remove), iconSize: 18),
        SizedBox(
          width: 86,
          child: Text(_text, textAlign: TextAlign.center, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w600)),
        ),
        IconButton.outlined(
            onPressed: value >= max ? null : () => onChanged(_clamp(value + step)), icon: const Icon(Icons.add), iconSize: 18),
      ]),
    );
  }
}

/// Scan for farm WiFi networks and give the controller the password.
class _WifiSheet extends StatefulWidget {
  const _WifiSheet({required this.conn});
  final HouseConn conn;
  @override
  State<_WifiSheet> createState() => _WifiSheetState();
}

class _WifiSheetState extends State<_WifiSheet> {
  List<Map<String, dynamic>> _nets = [];
  bool _scanning = false;
  StreamSubscription? _sub;
  final _ssid = TextEditingController();
  final _pass = TextEditingController();

  @override
  void initState() {
    super.initState();
    _sub = widget.conn.events.listen((m) {
      if (m['type'] == 'wifi_scan_result') {
        final d = m['data'];
        final list = d is List ? d : (d is Map && d['networks'] is List ? d['networks'] : []);
        setState(() {
          _scanning = false;
          _nets = [for (final x in list) Map<String, dynamic>.from(x)];
        });
      }
    });
    _scan();
  }

  void _scan() {
    if (widget.conn.via != 'Local' && widget.conn.via != 'Demo') return;
    setState(() => _scanning = true);
    widget.conn.send({'cmd': 'wifi_scan'});
    Future.delayed(const Duration(seconds: 25), () {
      if (mounted && _scanning) setState(() => _scanning = false);
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final local = widget.conn.via == 'Local' || widget.conn.via == 'Demo';
    return Padding(
      padding: EdgeInsets.fromLTRB(16, 16, 16, MediaQuery.of(context).viewInsets.bottom + 16),
      child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Expanded(child: Text('Farm WiFi', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w600))),
          if (local)
            IconButton(
                onPressed: _scanning ? null : _scan,
                icon: _scanning
                    ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.refresh)),
        ]),
        if (!local)
          const Padding(
            padding: EdgeInsets.only(bottom: 8),
            child: Text('Scanning works only when the phone is on the controller\'s own WiFi. You can still type the name.'),
          ),
        ConstrainedBox(
          constraints: const BoxConstraints(maxHeight: 220),
          child: ListView(shrinkWrap: true, children: [
            for (final n in _nets)
              ListTile(
                dense: true,
                leading: Icon(n['open'] == true ? Icons.wifi : Icons.wifi_lock),
                title: Text(n['ssid'] ?? ''),
                trailing: SignalBars(rssi: (n['rssi'] as num?)?.toInt() ?? 0),
                onTap: () => setState(() => _ssid.text = n['ssid'] ?? ''),
              ),
          ]),
        ),
        TextField(controller: _ssid, decoration: const InputDecoration(labelText: 'WiFi name')),
        TextField(controller: _pass, decoration: const InputDecoration(labelText: 'Password'), obscureText: true),
        const SizedBox(height: 12),
        Text('The controller keeps its own WiFi on until the farm WiFi has worked for a minute, '
            'and keeps misting the whole time.', style: TextStyle(fontSize: 12, color: Theme.of(context).colorScheme.onSurfaceVariant)),
        const SizedBox(height: 12),
        SizedBox(
          width: double.infinity,
          child: FilledButton(
            onPressed: () async {
              final r = await runCmd(context, widget.conn.send({'cmd': 'wifi_config', 'ssid': _ssid.text.trim(), 'pass': _pass.text}),
                  okMsg: 'Saved — controller is connecting');
              if (r['ok'] == true && context.mounted) Navigator.pop(context);
            },
            child: const Text('Connect controller'),
          ),
        ),
      ]),
    );
  }
}
