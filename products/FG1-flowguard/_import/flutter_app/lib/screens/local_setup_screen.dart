import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';

/// Device setup/provisioning over the local WebSocket connection —
/// replaces the old BLE-based flow. Since there's no BLE radio involved
/// anymore, the phone has to actually join the device's WiFi network
/// first (system WiFi settings, not something Flutter can do for the
/// user on iOS, and only awkwardly on Android) — this screen guides that
/// step, then talks to ws://192.168.4.1/ws the same way LocalService
/// always does.
class LocalSetupScreen extends ConsumerStatefulWidget {
  const LocalSetupScreen({super.key});
  @override
  ConsumerState<LocalSetupScreen> createState() => _LocalSetupScreenState();
}

class _LocalSetupScreenState extends ConsumerState<LocalSetupScreen> {
  bool _connected = false;
  bool _connecting = false;
  StreamSubscription? _respSub;
  Map<String, dynamic>? _deviceInfo;

  final _wifiSsidCtrl = TextEditingController();
  final _wifiPassCtrl = TextEditingController();
  final _calibrationCtrl = TextEditingController(text: '450');

  @override
  void dispose() {
    _respSub?.cancel();
    _wifiSsidCtrl.dispose();
    _wifiPassCtrl.dispose();
    _calibrationCtrl.dispose();
    super.dispose();
  }

  @override
  void initState() {
    super.initState();
    // Auto-connect (idempotent — instant no-op if the Dashboard already
    // has this connection open). Previously this screen required an
    // explicit tap on "Connect" even when the app was already talking to
    // the device elsewhere, which looked like every button here was
    // broken if you didn't happen to tap it first.
    WidgetsBinding.instance.addPostFrameCallback((_) => _connect());
  }

  Future<void> _connect() async {
    setState(() => _connecting = true);
    final local = ref.read(localServiceProvider);
    _respSub ??= local.responseStream.listen(_onResponse);
    final ok = await local.connect();
    setState(() {
      _connected = ok;
      _connecting = false;
    });
    if (!ok && mounted) {
      _snack('Could not connect — make sure your phone is on the device\'s WiFi network');
    }
  }

  void _onResponse(Map<String, dynamic> msg) {
    final cmd = msg['cmd'] as String?;
    final ok  = msg['ok'] == true;
    if (cmd == 'device_info' && ok) {
      setState(() => _deviceInfo = msg['data'] as Map<String, dynamic>?);
      return;
    }
    if (!mounted) return;
    _snack(ok ? '$cmd: saved' : '$cmd: failed');
  }

  void _snack(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  void _send(Map<String, dynamic> payload) {
    if (!_connected) {
      _snack('Not connected — connect to the device WiFi first');
      return;
    }
    ref.read(localServiceProvider).sendRaw(payload);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        title: Text('Local Device Setup',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface, fontWeight: FontWeight.w600)),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _stepCard(
            step: '1',
            title: 'Join the device\'s WiFi',
            body: 'Open your phone\'s WiFi settings and connect to the network '
                'starting with "SWC_001_" (shown on the device, or in the app '
                'when it\'s already connected). Default password is set in the '
                'device firmware.',
          ),
          const SizedBox(height: 12),
          _stepCard(
            step: '2',
            title: 'Connect',
            body: 'Once your phone shows it\'s joined that network, tap Connect below.',
            trailing: ElevatedButton.icon(
              onPressed: _connecting ? null : _connect,
              icon: _connecting
                  ? const SizedBox(width: 16, height: 16,
                      child: CircularProgressIndicator(strokeWidth: 2))
                  : Icon(_connected ? Icons.check_circle : Icons.wifi_tethering),
              label: Text(_connected ? 'Connected' : 'Connect'),
              style: ElevatedButton.styleFrom(
                backgroundColor: _connected ? const Color(0xFF4CAF50) : const Color(0xFF2196F3),
                foregroundColor: Colors.white,
              ),
            ),
          ),
          const SizedBox(height: 24),

          _sectionTitle('Home WiFi (for cloud mode)'),
          _card(child: Column(children: [
            TextField(controller: _wifiSsidCtrl,
                decoration: const InputDecoration(labelText: 'SSID')),
            const SizedBox(height: 8),
            TextField(controller: _wifiPassCtrl, obscureText: true,
                decoration: const InputDecoration(labelText: 'Password')),
            const SizedBox(height: 12),
            SizedBox(width: double.infinity, child: ElevatedButton(
              onPressed: () => _send({
                'cmd': 'wifi_config',
                'ssid': _wifiSsidCtrl.text,
                'pass': _wifiPassCtrl.text,
              }),
              child: const Text('Save WiFi Credentials'),
            )),
          ])),
          const SizedBox(height: 16),

          // MQTT broker config UI intentionally removed — mqtt.grty.co.in
          // is now the fixed broker for both firmware and app, not a
          // user-changeable setting. The device's mqtt_config command
          // still exists in firmware for direct use (e.g. via curl) if
          // it's ever needed, just not exposed here.

          _sectionTitle('Device Time'),
          _card(child: SizedBox(width: double.infinity, child: ElevatedButton.icon(
            onPressed: () {
              final now = DateTime.now();
              // Same encoding as _deviceEpoch() in mqtt_service.dart/
              // local_service.dart: re-interpret the phone's local
              // wall-clock fields AS IF they were UTC, rather than a true
              // UTC conversion (.toUtc()). The firmware's DS3231 stores
              // IST wall-clock fields directly, so it expects this
              // encoding — a real UTC conversion shifted the synced time
              // by the IST offset (5.5 hours), which is exactly the bug
              // this fixes (22:17 IST was landing on the device as 16:47).
              final deviceEpoch = DateTime.utc(
                now.year, now.month, now.day,
                now.hour, now.minute, now.second,
              ).millisecondsSinceEpoch ~/ 1000;
              _send({'cmd': 'rtc_sync', 'unix': deviceEpoch});
            },
            icon: const Icon(Icons.access_time),
            label: const Text('Sync Time From Phone'),
          ))),
          const SizedBox(height: 16),

          _sectionTitle('Flow Sensor Calibration'),
          _card(child: Column(children: [
            TextField(controller: _calibrationCtrl,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(labelText: 'Pulses per liter')),
            const SizedBox(height: 12),
            SizedBox(width: double.infinity, child: ElevatedButton(
              onPressed: () => _send({
                'cmd': 'calibrate',
                'ppl': int.tryParse(_calibrationCtrl.text) ?? 450,
              }),
              child: const Text('Save Calibration'),
            )),
          ])),
          const SizedBox(height: 16),

          _sectionTitle('Diagnostics'),
          _card(child: Column(children: [
            SizedBox(width: double.infinity, child: OutlinedButton.icon(
              onPressed: () => _send({'cmd': 'relay_test'}),
              icon: const Icon(Icons.bolt),
              label: const Text('Test Relay (5s pulse)'),
            )),
            const SizedBox(height: 8),
            SizedBox(width: double.infinity, child: OutlinedButton.icon(
              onPressed: () => _send({'cmd': 'device_info'}),
              icon: const Icon(Icons.info_outline),
              label: const Text('Get Device Info'),
            )),
            if (_deviceInfo != null) ...[
              const SizedBox(height: 12),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: const Color(0xFFF5F6FA),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(_deviceInfo.toString(),
                    style: const TextStyle(fontSize: 12, fontFamily: 'monospace')),
              ),
            ],
          ])),
          const SizedBox(height: 16),

          _sectionTitle('Danger Zone'),
          _card(child: SizedBox(width: double.infinity, child: OutlinedButton.icon(
            style: OutlinedButton.styleFrom(foregroundColor: Colors.red),
            onPressed: () async {
              final confirmed = await showDialog<bool>(
                context: context,
                builder: (ctx) => AlertDialog(
                  title: const Text('Factory Reset?'),
                  content: const Text(
                      'This erases all saved cycles, WiFi/MQTT credentials, and '
                      'calibration on the device. This cannot be undone.'),
                  actions: [
                    TextButton(onPressed: () => Navigator.pop(ctx, false),
                        child: const Text('Cancel')),
                    TextButton(onPressed: () => Navigator.pop(ctx, true),
                        child: const Text('Reset', style: TextStyle(color: Colors.red))),
                  ],
                ),
              );
              if (confirmed == true) _send({'cmd': 'factory_reset'});
            },
            icon: const Icon(Icons.warning_amber),
            label: const Text('Factory Reset Device'),
          ))),
        ],
      ),
    );
  }

  Widget _sectionTitle(String text) => Padding(
    padding: const EdgeInsets.only(left: 4, bottom: 8),
    child: Text(text.toUpperCase(),
        style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 11,
            fontWeight: FontWeight.w600, letterSpacing: 0.5)),
  );

  Widget _card({required Widget child}) => Container(
    width: double.infinity,
    padding: const EdgeInsets.all(16),
    decoration: BoxDecoration(
      color: Theme.of(context).cardColor,
      borderRadius: BorderRadius.circular(12),
      boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
          blurRadius: 8, offset: const Offset(0, 2))],
    ),
    child: child,
  );

  Widget _stepCard({required String step, required String title,
                    required String body, Widget? trailing}) => _card(
    child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        CircleAvatar(radius: 12, backgroundColor: const Color(0xFF2196F3),
            child: Text(step, style: const TextStyle(color: Colors.white, fontSize: 12))),
        const SizedBox(width: 8),
        Text(title, style: const TextStyle(fontWeight: FontWeight.w600)),
      ]),
      const SizedBox(height: 8),
      Text(body, style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 13)),
      if (trailing != null) ...[
        const SizedBox(height: 12),
        trailing,
      ],
    ]),
  );
}
