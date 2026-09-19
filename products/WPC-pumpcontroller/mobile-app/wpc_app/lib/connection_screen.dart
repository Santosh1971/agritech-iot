import 'package:flutter/material.dart';
import 'api.dart';
import 'backend.dart';

/// How the app reaches a Master (Local vs Cloud), which installation is
/// selected, and -- while on the Master's own WiFi -- which farm WiFi the
/// Master should use to reach the internet.
class ConnectionScreen extends StatefulWidget {
  const ConnectionScreen({super.key});

  @override
  State<ConnectionScreen> createState() => _ConnectionScreenState();
}

class _ConnectionScreenState extends State<ConnectionScreen> {
  final _ssid = TextEditingController();
  final _pass = TextEditingController();
  Map<String, dynamic>? _localStatus;
  String? _localError;
  bool _busy = false;
  bool _showPass = false;

  @override
  void initState() {
    super.initState();
    Backend.instance.addListener(_rebuild);
    _refreshLocal();
  }

  @override
  void dispose() {
    Backend.instance.removeListener(_rebuild);
    _ssid.dispose();
    _pass.dispose();
    super.dispose();
  }

  void _rebuild() {
    if (mounted) setState(() {});
  }

  void _snack(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _refreshLocal() async {
    try {
      final s = await WpcApi.getLocalStatus();
      if (!mounted) return;
      setState(() {
        _localStatus = s;
        _localError = null;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() {
        _localStatus = null;
        _localError = 'Not connected to a Master\'s WiFi (WPC-Master-XXXXXXXX)';
      });
    }
  }

  static String _stripId(String raw) =>
      raw.replaceFirst(RegExp(r'^0x', caseSensitive: false), '').toUpperCase();

  Future<void> _addFromLocal() async {
    final id = _stripId((_localStatus?['masterId'] as String?) ?? '');
    if (id.length != 8) return;
    final name = await _askName(initial: '');
    if (name == null) return;
    await Backend.instance.addMaster(id, name);
    _snack('Added Master $id');
  }

  Future<String?> _askName({required String initial}) {
    final c = TextEditingController(text: initial);
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Name this installation'),
        content: TextField(
          controller: c,
          autofocus: true,
          decoration: const InputDecoration(hintText: 'e.g. North Farm'),
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')),
          TextButton(onPressed: () => Navigator.pop(ctx, c.text.trim()), child: const Text('Save')),
        ],
      ),
    );
  }

  Future<void> _addManual() async {
    final idC = TextEditingController();
    final nameC = TextEditingController();
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Add Master'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: idC,
              textCapitalization: TextCapitalization.characters,
              maxLength: 8,
              decoration: const InputDecoration(
                labelText: 'Master ID (8 characters)',
                helperText: 'Shown on the Status screen and in the WPC-Master-XXXXXXXX WiFi name',
              ),
            ),
            TextField(
              controller: nameC,
              decoration: const InputDecoration(labelText: 'Name (optional)', hintText: 'e.g. North Farm'),
            ),
          ],
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          TextButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Add')),
        ],
      ),
    );
    if (ok != true) return;
    final id = idC.text.trim().toUpperCase();
    if (!RegExp(r'^[0-9A-F]{8}$').hasMatch(id)) {
      _snack('Master ID must be exactly 8 characters (0-9, A-F)');
      return;
    }
    await Backend.instance.addMaster(id, nameC.text);
  }

  Future<void> _saveWifi() async {
    final ssid = _ssid.text.trim();
    if (ssid.isEmpty) {
      _snack('Enter the farm WiFi name');
      return;
    }
    setState(() => _busy = true);
    try {
      await WpcApi.setMasterWifi(ssid, _pass.text);
      _snack('Sent. The Master is joining the farm WiFi -- this can take about 30 seconds.');
    } catch (e) {
      // The Master can drop its own access point for a moment while it joins
      // the router, which shows up here as a timeout even though it worked.
      _snack('No confirmation ($e). Wait a moment and refresh to check.');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
    await Future.delayed(const Duration(seconds: 6));
    await _refreshLocal();
  }

  @override
  Widget build(BuildContext context) {
    final b = Backend.instance;
    final wifi = (_localStatus?['wifi'] as Map<String, dynamic>?) ?? {};
    final localCloudUp = _localStatus?['cloud'] == true;

    return Scaffold(
      appBar: AppBar(title: const Text('Connection')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text('How do you want to connect?', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          SegmentedButton<LinkMode>(
            segments: const [
              ButtonSegment(
                  value: LinkMode.local, label: Text('Local'), icon: Icon(Icons.wifi)),
              ButtonSegment(
                  value: LinkMode.cloud, label: Text('Cloud'), icon: Icon(Icons.cloud_outlined)),
            ],
            selected: {b.mode},
            onSelectionChanged: (s) => b.setMode(s.first),
          ),
          const SizedBox(height: 6),
          Text(
            b.mode == LinkMode.local
                ? "Local: your phone is on the Master's own WiFi (WPC-Master-XXXXXXXX). Works with no internet."
                : 'Cloud: monitor and control from anywhere over the internet. The Master must be on the farm WiFi.',
            style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
          ),
          const SizedBox(height: 24),

          Text('My installations', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 4),
          if (b.masters.isEmpty)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 12),
              child: Text('None yet. Add the Master ID of each installation (needed for Cloud mode).'),
            ),
          ...b.masters.map((m) => ListTile(
                contentPadding: EdgeInsets.zero,
                leading: Icon(
                  b.activeId == m.id ? Icons.radio_button_checked : Icons.radio_button_off,
                  color: b.activeId == m.id ? Theme.of(context).colorScheme.primary : Colors.grey,
                ),
                title: Text(m.label),
                subtitle: Text(m.id),
                onTap: () => b.setActive(m.id),
                trailing: IconButton(
                  icon: const Icon(Icons.delete_outline, color: Colors.red),
                  onPressed: () => b.removeMaster(m.id),
                ),
              )),
          Row(
            children: [
              OutlinedButton.icon(
                onPressed: _addManual,
                icon: const Icon(Icons.add),
                label: const Text('Add Master'),
              ),
              const SizedBox(width: 8),
              if (_localStatus != null)
                OutlinedButton.icon(
                  onPressed: _addFromLocal,
                  icon: const Icon(Icons.wifi_tethering),
                  label: const Text('Add the one I\'m on'),
                ),
            ],
          ),

          if (b.mode == LinkMode.cloud) ...[
            const SizedBox(height: 24),
            Text('Cloud status', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 4),
            _statusRow('Broker', b.brokerConnected ? 'connected' : (b.cloudError ?? 'not connected'), b.brokerConnected),
            _statusRow(
                'Master',
                b.masterOnline == null ? 'unknown yet' : (b.masterOnline! ? 'online' : 'offline'),
                b.masterOnline == true),
          ],

          const SizedBox(height: 24),
          Text('Master internet (farm WiFi)', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 4),
          Text(
            "Connect your phone to the Master's own WiFi first, then enter the farm WiFi it should use "
            'to reach the internet.',
            style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
          ),
          const SizedBox(height: 8),
          if (_localStatus == null)
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.grey.shade100,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Row(
                children: [
                  const Icon(Icons.wifi_off, color: Colors.grey),
                  const SizedBox(width: 8),
                  Expanded(child: Text(_localError ?? 'Checking...')),
                  TextButton(onPressed: _refreshLocal, child: const Text('Retry')),
                ],
              ),
            )
          else ...[
            _statusRow(
                'Master',
                '${_stripId((_localStatus!['masterId'] as String?) ?? '')}'
                    '  (fw ${_localStatus!['fw'] ?? '?'})',
                true),
            _statusRow(
                'Farm WiFi',
                wifi['configured'] == true
                    ? '${wifi['ssid']} - ${wifi['connected'] == true ? 'connected (${wifi['ip']})' : 'not connected'}'
                    : 'not set up',
                wifi['connected'] == true),
            _statusRow('Cloud', localCloudUp ? 'connected' : 'not connected', localCloudUp),
            const SizedBox(height: 8),
            TextField(
              controller: _ssid,
              decoration: const InputDecoration(
                labelText: 'Farm WiFi name (SSID)',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _pass,
              obscureText: !_showPass,
              decoration: InputDecoration(
                labelText: 'Farm WiFi password',
                border: const OutlineInputBorder(),
                suffixIcon: IconButton(
                  icon: Icon(_showPass ? Icons.visibility_off : Icons.visibility),
                  onPressed: () => setState(() => _showPass = !_showPass),
                ),
              ),
            ),
            const SizedBox(height: 8),
            Row(
              children: [
                FilledButton(
                  onPressed: _busy ? null : _saveWifi,
                  child: _busy
                      ? const SizedBox(
                          width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2))
                      : const Text('Save to Master'),
                ),
                const SizedBox(width: 8),
                TextButton(onPressed: _refreshLocal, child: const Text('Refresh')),
              ],
            ),
          ],
        ],
      ),
    );
  }

  Widget _statusRow(String label, String value, bool good) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(good ? Icons.check_circle : Icons.error_outline,
              size: 16, color: good ? Colors.green : Colors.orange),
          const SizedBox(width: 6),
          SizedBox(width: 80, child: Text(label, style: const TextStyle(fontWeight: FontWeight.w500))),
          Expanded(child: Text(value)),
        ],
      ),
    );
  }
}
