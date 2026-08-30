import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../models/device_status.dart';

/// Provisioning screen — always uses the LOCAL transport regardless of
/// the app's mode switch, since every action here (WiFi join, RTC sync,
/// factory reset, relay naming) inherently requires being on the
/// device's own network already. There's deliberately no "device
/// address" field: SoftAP is always 192.168.4.1, and LocalService
/// reconnects to it on its own.
class LocalSetupScreen extends ConsumerStatefulWidget {
  const LocalSetupScreen({super.key});
  @override
  ConsumerState<LocalSetupScreen> createState() => _LocalSetupScreenState();
}

class _LocalSetupScreenState extends ConsumerState<LocalSetupScreen> {
  final _ssidController = TextEditingController();
  final _passController = TextEditingController();
  final _pumpNameController = TextEditingController();
  final _dosingNameController = TextEditingController();
  final List<TextEditingController> _valveNameControllers =
      List.generate(4, (_) => TextEditingController());

  StreamSubscription<Map<String, dynamic>>? _responseSub;
  bool _scanning = false;
  List<Map<String, dynamic>> _scanResults = [];
  bool _namesInitialized = false;

  @override
  void initState() {
    super.initState();
    _responseSub = ref.read(localServiceProvider).responseStream.listen(_onResponse);
  }

  @override
  void dispose() {
    _responseSub?.cancel();
    _ssidController.dispose();
    _passController.dispose();
    _pumpNameController.dispose();
    _dosingNameController.dispose();
    for (final c in _valveNameControllers) {
      c.dispose();
    }
    super.dispose();
  }

  void _onResponse(Map<String, dynamic> msg) {
    if (msg['cmd'] == 'wifi_scan' && msg['data'] is List) {
      final results = (msg['data'] as List).cast<Map<String, dynamic>>();
      results.sort((a, b) => (b['rssi'] as num).compareTo(a['rssi'] as num));
      if (mounted) setState(() { _scanResults = results; _scanning = false; });
    }
  }

  void _snack(String text) => ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(text)));

  void _startScan() {
    setState(() { _scanning = true; _scanResults = []; });
    ref.read(localServiceProvider).scanWifi();
    // Firmware retries internally on a failed scan; if nothing comes
    // back at all within a generous window, stop spinning rather than
    // leaving the user staring at a spinner forever.
    Future.delayed(const Duration(seconds: 12), () {
      if (mounted && _scanning) setState(() => _scanning = false);
    });
  }

  void _prefillNamesIfNeeded(DeviceStatus status) {
    if (_namesInitialized) return;
    _namesInitialized = true;
    final names = status.relayNames;
    _pumpNameController.text = names.pump;
    _dosingNameController.text = names.dosing;
    for (int i = 0; i < 4; i++) {
      _valveNameControllers[i].text = names.valveName(i);
    }
  }

  void _saveRelayNames() {
    ref.read(localServiceProvider).setRelayNames(
          pump: _pumpNameController.text.trim().isEmpty ? 'Pump' : _pumpNameController.text.trim(),
          dosing: _dosingNameController.text.trim().isEmpty ? 'Dosing' : _dosingNameController.text.trim(),
          valves: List.generate(4, (i) {
            final t = _valveNameControllers[i].text.trim();
            return t.isEmpty ? 'Valve ${i + 1}' : t;
          }),
        );
    _snack('Relay names saved');
  }

  Future<void> _confirmFactoryReset() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Factory Reset?'),
        content: const Text(
            'This erases all saved programs, WiFi credentials, and relay names, and reboots the device. This cannot be undone.'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Reset', style: TextStyle(color: Colors.red))),
        ],
      ),
    );
    if (confirmed == true) {
      ref.read(localServiceProvider).factoryReset();
      _snack('Factory reset sent — device is rebooting');
    }
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(localDeviceStatusProvider).whenData(_prefillNamesIfNeeded);
    final connected = ref.watch(deviceConnectedProvider);
    final log = ref.watch(localDebugLogProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Local Device Setup', style: TextStyle(fontWeight: FontWeight.w600)), centerTitle: true),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _Card(children: [
            Row(children: [
              Icon(connected ? Icons.check_circle : Icons.error_outline, color: connected ? Colors.green : Colors.grey, size: 18),
              const SizedBox(width: 8),
              Text(connected ? 'Connected to device' : 'Not connected', style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
              const Spacer(),
              TextButton(onPressed: () => ref.read(localServiceProvider).retryNow(), child: const Text('Retry')),
            ]),
            const SizedBox(height: 6),
            Text('Live connection log — shows exactly what\'s happening without needing a laptop/serial cable.',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 11)),
            const SizedBox(height: 8),
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(8),
              constraints: const BoxConstraints(maxHeight: 160),
              decoration: BoxDecoration(color: Colors.black87, borderRadius: BorderRadius.circular(8)),
              child: log.isEmpty
                  ? const Text('(no events yet)', style: TextStyle(color: Colors.grey, fontFamily: 'monospace', fontSize: 11))
                  : ListView.builder(
                      reverse: true,
                      shrinkWrap: true,
                      itemCount: log.length,
                      itemBuilder: (_, i) => Text(log[log.length - 1 - i],
                          style: const TextStyle(color: Color(0xFF7CFC7C), fontFamily: 'monospace', fontSize: 11)),
                    ),
            ),
            if (!connected) ...[
              const SizedBox(height: 8),
              Text(
                'If the log shows "Connect failed" or nothing at all even '
                'though your phone is on the device\'s WM1_XXXX WiFi: try '
                'turning off mobile data (or airplane mode + WiFi back on). '
                'Some phones route apps over cellular instead of a WiFi '
                'network that has no internet, which silently breaks this.',
                style: TextStyle(color: Colors.orange.shade800, fontSize: 11),
              ),
            ],
          ]),
          const SizedBox(height: 16),
          _Card(children: [
            Row(children: [
              _Label('Join Farm WiFi'),
              const Spacer(),
              TextButton.icon(
                onPressed: connected ? _startScan : null,
                icon: _scanning
                    ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2))
                    : const Icon(Icons.wifi_find, size: 18),
                label: Text(_scanning ? 'Scanning...' : 'Scan'),
              ),
            ]),
            Text('Send the device your real WiFi credentials so it can join your network and reach the cloud broker in Cloud mode.',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 12)),
            if (_scanResults.isNotEmpty) ...[
              const SizedBox(height: 8),
              ConstrainedBox(
                constraints: const BoxConstraints(maxHeight: 220),
                child: ListView.separated(
                  shrinkWrap: true,
                  itemCount: _scanResults.length,
                  separatorBuilder: (_, __) => const Divider(height: 1),
                  itemBuilder: (_, i) {
                    final r = _scanResults[i];
                    final ssid = r['ssid'] as String? ?? '';
                    final rssi = (r['rssi'] as num?)?.toInt() ?? -100;
                    final open = r['open'] as bool? ?? false;
                    return ListTile(
                      dense: true,
                      leading: Icon(_wifiIcon(rssi), size: 20),
                      title: Text(ssid.isEmpty ? '(hidden)' : ssid),
                      trailing: Icon(open ? Icons.lock_open : Icons.lock, size: 16, color: Colors.grey),
                      onTap: () => setState(() => _ssidController.text = ssid),
                    );
                  },
                ),
              ),
            ],
            const SizedBox(height: 10),
            TextField(controller: _ssidController, decoration: _dec('WiFi SSID')),
            const SizedBox(height: 8),
            TextField(controller: _passController, decoration: _dec('WiFi Password'), obscureText: true),
            const SizedBox(height: 10),
            SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: connected
                    ? () {
                        ref.read(localServiceProvider).sendRaw({
                          'cmd': 'wifi_config',
                          'ssid': _ssidController.text.trim(),
                          'password': _passController.text,
                        });
                        _snack('Sent — watch the device for a WiFi connect log');
                      }
                    : null,
                child: const Text('Send WiFi Credentials'),
              ),
            ),
          ]),
          const SizedBox(height: 16),
          _Card(children: [
            _Label('Device Time'),
            Text('Syncs the device\'s RTC from your phone\'s current time — schedules won\'t trigger until the RTC is set.',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 12)),
            const SizedBox(height: 10),
            SizedBox(
              width: double.infinity,
              child: OutlinedButton.icon(
                onPressed: connected ? () { ref.read(localServiceProvider).syncRtcFromPhone(); _snack('Time synced from phone'); } : null,
                icon: const Icon(Icons.schedule),
                label: const Text('Sync Time From Phone'),
              ),
            ),
          ]),
          const SizedBox(height: 16),
          _Card(children: [
            _Label('Rename Relays'),
            Text('Display names only — what each relay actually does (pump, dosing, or a valve) doesn\'t change.',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 12)),
            const SizedBox(height: 10),
            TextField(controller: _pumpNameController, decoration: _dec('Pump (RL1)')),
            const SizedBox(height: 8),
            TextField(controller: _dosingNameController, decoration: _dec('Dosing (RL2)')),
            for (int i = 0; i < 4; i++) ...[
              const SizedBox(height: 8),
              TextField(controller: _valveNameControllers[i], decoration: _dec('Valve ${i + 1} (RL${i + 3})')),
            ],
            const SizedBox(height: 10),
            SizedBox(
              width: double.infinity,
              child: FilledButton(onPressed: connected ? _saveRelayNames : null, child: const Text('Save Relay Names')),
            ),
          ]),
          const SizedBox(height: 16),
          _Card(children: [
            _Label('Device Info'),
            OutlinedButton(
              onPressed: connected ? () => ref.read(localServiceProvider).sendRaw({'cmd': 'device_info'}) : null,
              child: const Text('Request Device Info'),
            ),
          ]),
          const SizedBox(height: 16),
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Colors.red.withOpacity(0.06),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(color: Colors.red.withOpacity(0.3)),
            ),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Text('DANGER ZONE', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w700, color: Colors.red, letterSpacing: 0.5)),
              const SizedBox(height: 8),
              const Text('Erases all saved programs, WiFi credentials, and relay names. Cannot be undone.',
                  style: TextStyle(fontSize: 12, color: Colors.redAccent)),
              const SizedBox(height: 10),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: connected ? _confirmFactoryReset : null,
                  style: OutlinedButton.styleFrom(foregroundColor: Colors.red, side: const BorderSide(color: Colors.red)),
                  icon: const Icon(Icons.warning_amber),
                  label: const Text('Factory Reset'),
                ),
              ),
            ]),
          ),
        ],
      ),
    );
  }

  IconData _wifiIcon(int rssi) {
    if (rssi >= -60) return Icons.wifi;
    if (rssi >= -75) return Icons.wifi_2_bar;
    return Icons.wifi_1_bar;
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
  Widget build(BuildContext context) => Text(text, style: const TextStyle(fontWeight: FontWeight.w500, fontSize: 14));
}
