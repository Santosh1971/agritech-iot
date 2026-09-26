import 'dart:convert';
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
  bool _scanning = false;
  List<Map<String, dynamic>> _networks = [];
  String? _scanNote;

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
    // Used exactly as entered or picked: never trim, because a name may legitimately contain
    // (even start or end with) spaces and the router matches it byte for byte.
    final ssid = _ssid.text;
    if (ssid.trim().isEmpty) {
      _snack('Enter or pick the farm WiFi name');
      return;
    }
    if (_bytes(ssid) > 32) {
      _snack('WiFi name is too long (${_bytes(ssid)} of 32 bytes allowed)');
      return;
    }
    if (_bytes(_pass.text) > 63) {
      _snack('Password is too long (${_bytes(_pass.text)} of 63 bytes allowed)');
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

  // SSIDs are limited by 802.11 to 32 BYTES (not characters), so count UTF-8 bytes.
  static int _bytes(String s) => utf8.encode(s).length;

  // Our own devices' access points are noise in a "pick the farm WiFi" list.
  static bool _isWpcDevice(String ssid) => ssid.startsWith('WPC-');

  Future<void> _scan() async {
    setState(() {
      _scanning = true;
      _scanNote = null;
    });
    try {
      await WpcApi.startWifiScan();
      // The scan can briefly interrupt the phone's link to the Master's access point,
      // so a few failed polls in a row are expected and not fatal.
      var failures = 0;
      for (var i = 0; i < 24; i++) {
        await Future.delayed(const Duration(milliseconds: 1000));
        try {
          final r = await WpcApi.getWifiScan();
          failures = 0;
          if (r['scanning'] == false) {
            final list = (r['networks'] as List? ?? [])
                .map((e) => Map<String, dynamic>.from(e as Map))
                .where((n) => !_isWpcDevice((n['ssid'] as String?) ?? ''))
                .toList();
            if (!mounted) return;
            setState(() {
              _networks = list;
              _scanNote = list.isEmpty ? 'No networks found. Move closer to the router and scan again.' : null;
            });
            return;
          }
        } catch (_) {
          if (++failures >= 6) rethrow;
        }
      }
      if (mounted) setState(() => _scanNote = 'The scan took too long. Try again.');
    } catch (e) {
      if (mounted) {
        setState(() => _scanNote = 'Scan failed ($e). Make sure your phone is on the Master\'s WiFi and try again.');
      }
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  IconData _signalIcon(int rssi) {
    if (rssi >= -60) return Icons.signal_wifi_4_bar;
    if (rssi >= -70) return Icons.network_wifi_3_bar;
    if (rssi >= -80) return Icons.network_wifi_2_bar;
    return Icons.network_wifi_1_bar;
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

          const SizedBox(height: 24),
          Text('Dashboard display', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 4),
          Text(
            "Hide a sensor here if this installation doesn't have it wired -- the Master/Pump "
            'still reports it either way, this only affects what the app shows.',
            style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Power status'),
            subtitle: const Text('No-Power input, at the Master and each Pump'),
            value: b.showPowerStatus,
            onChanged: (v) => b.setShowPowerStatus(v),
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('Water flow / pump running'),
            subtitle: const Text("Each Pump's flow input -- confirms it is actually running"),
            value: b.showWaterFlow,
            onChanged: (v) => b.setShowWaterFlow(v),
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
                    ? '${wifi['ssid']} - ${wifi['connected'] == true ? 'connected (${wifi['ip']})' : _wifiStateText(wifi['state'] as String?)}'
                    : 'not set up',
                wifi['connected'] == true),
            _statusRow('Cloud', localCloudUp ? 'connected' : 'not connected', localCloudUp),
            const SizedBox(height: 8),
            OutlinedButton.icon(
              onPressed: _scanning ? null : _scan,
              icon: _scanning
                  ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : const Icon(Icons.wifi_find),
              label: Text(_scanning ? 'Scanning...' : 'Scan for WiFi networks'),
            ),
            Padding(
              padding: const EdgeInsets.only(top: 4),
              child: Text(
                'The scan can briefly interrupt your connection to the Master. If it does, wait a moment.',
                style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
              ),
            ),
            if (_scanNote != null)
              Padding(
                padding: const EdgeInsets.only(top: 6),
                child: Text(_scanNote!, style: TextStyle(color: Colors.orange.shade800, fontSize: 12)),
              ),
            if (_networks.isNotEmpty)
              Container(
                margin: const EdgeInsets.only(top: 8),
                constraints: const BoxConstraints(maxHeight: 280),
                decoration: BoxDecoration(
                  border: Border.all(color: Colors.grey.shade300),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: ListView.separated(
                  shrinkWrap: true,
                  itemCount: _networks.length,
                  separatorBuilder: (_, __) => const Divider(height: 1),
                  itemBuilder: (context, i) {
                    final n = _networks[i];
                    final name = (n['ssid'] as String?) ?? '';
                    final rssi = (n['rssi'] as num?)?.toInt() ?? -100;
                    final open = n['open'] == true;
                    return ListTile(
                      dense: true,
                      leading: Icon(_signalIcon(rssi)),
                      // Long names wrap onto a second line instead of being cut off.
                      title: Text(name, maxLines: 2, overflow: TextOverflow.ellipsis),
                      subtitle: Text(open ? 'Open (no password)' : 'Secured  -  $rssi dBm'),
                      trailing: _ssid.text == name ? const Icon(Icons.check_circle, color: Colors.green) : null,
                      onTap: () => setState(() => _ssid.text = name),
                    );
                  },
                ),
              ),
            const SizedBox(height: 12),
            // Autocorrect/suggestions/capitalisation are off on purpose: the keyboard must never
            // "fix" a network name or password.
            TextField(
              controller: _ssid,
              autocorrect: false,
              enableSuggestions: false,
              textCapitalization: TextCapitalization.none,
              maxLines: 1,
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                labelText: 'Farm WiFi name (SSID)',
                border: const OutlineInputBorder(),
                helperText: 'Pick from the list, or type it (also for hidden networks). '
                    'Spaces are kept exactly. Max 32 bytes.',
                helperMaxLines: 2,
                counterText: '${_bytes(_ssid.text)}/32',
                errorText: _bytes(_ssid.text) > 32
                    ? 'Too long for a WiFi name'
                    : (_ssid.text.isNotEmpty && _ssid.text != _ssid.text.trim()
                        ? 'This name starts or ends with a space. Correct only if the network really has one.'
                        : null),
                errorMaxLines: 2,
              ),
            ),
            const SizedBox(height: 8),
            TextField(
              controller: _pass,
              obscureText: !_showPass,
              autocorrect: false,
              enableSuggestions: false,
              textCapitalization: TextCapitalization.none,
              decoration: InputDecoration(
                labelText: 'Farm WiFi password',
                border: const OutlineInputBorder(),
                helperText: 'Leave empty for an open network.',
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

  // Turns the Master's wifi.state (added 26 Sep 2026, see Cloud.h's wifiStateStr()) into something
  // an installer can act on -- a bare "not connected" gives no clue whether the network name is
  // wrong, the password is wrong, or the Master just can't reach that network from where it sits.
  static String _wifiStateText(String? state) {
    switch (state) {
      case 'no_ssid':
        return 'not connected - this network name was not found (check it\'s 2.4GHz and in range)';
      case 'connect_failed':
        return 'not connected - check the password';
      case 'connection_lost':
        return 'not connected - lost the connection';
      case 'disconnected':
      case 'connecting':
        return 'not connected - retrying';
      default:
        return 'not connected';
    }
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
