import 'dart:async';
import 'package:flutter/material.dart';
import 'api.dart';

class PumpScreen extends StatefulWidget {
  const PumpScreen({super.key});

  @override
  State<PumpScreen> createState() => _PumpScreenState();
}

class _PumpScreenState extends State<PumpScreen> {
  Map<String, dynamic>? _info;
  String? _error;
  bool _busy = false;
  bool _editing = false;

  final _masterIdController = TextEditingController();
  Timer? _timer;

  @override
  void initState() {
    super.initState();
    _fetch();
    _timer = Timer.periodic(const Duration(seconds: 3), (_) => _fetch());
  }

  @override
  void dispose() {
    _timer?.cancel();
    _masterIdController.dispose();
    super.dispose();
  }

  Future<void> _fetch() async {
    setState(() => _busy = true);
    try {
      final info = await WpcApi.getPumpInfo();
      if (!mounted) return;
      String masterId = (info['targetMasterId'] as String? ?? '');
      if (masterId.toLowerCase().startsWith('0x')) {
        masterId = masterId.substring(2);
      }
      setState(() {
        _info = info;
        _error = null;
        // don't stomp on what the user is actively typing while auto-refresh polls
        if (!_editing) _masterIdController.text = masterId;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _error =
          "Not reachable -- connect WiFi to the Pump's own network (WPC-Pump-XXXX), not the Master");
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _save() async {
    final masterId = _masterIdController.text.trim().toUpperCase();
    final hexPattern = RegExp(r'^[0-9A-F]{8}$');
    if (!hexPattern.hasMatch(masterId)) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Master ID must be exactly 8 characters (0-9, A-F)')),
      );
      return;
    }
    setState(() => _busy = true);
    try {
      await WpcApi.setPumpConfig(targetMasterId: masterId);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Saved -- Pump is rejoining under the new Master')),
        );
      }
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to save: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return RefreshIndicator(
      onRefresh: _fetch,
      child: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Container(
            padding: const EdgeInsets.all(12),
            margin: const EdgeInsets.only(bottom: 16),
            decoration: BoxDecoration(
              color: Colors.blue.shade50,
              border: Border.all(color: Colors.blue.shade200),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Icon(Icons.info_outline, color: Colors.blue.shade700),
                const SizedBox(width: 8),
                const Expanded(
                  child: Text(
                    "Connect your phone's WiFi to the Pump's own network "
                    "(WPC-Pump-XXXX) before using this screen -- not the Master's.",
                  ),
                ),
              ],
            ),
          ),

          if (_error != null)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 24),
              child: Column(
                children: [
                  const Icon(Icons.wifi_off, size: 48, color: Colors.grey),
                  const SizedBox(height: 12),
                  Text(_error!, textAlign: TextAlign.center),
                ],
              ),
            ),

          if (_info != null) ...[
            Text('Current Identity', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('Pump ID: ${_info!['pumpId']}'),
                    Text(
                      'Target Master: ${(_info!['targetMasterId'] as String? ?? '').replaceFirst(RegExp(r'^0x', caseSensitive: false), '')}',
                    ),
                    Text('Joined: ${_info!['joined'] == true ? 'Yes' : 'No'}'),
                    if (_info!['joined'] != true)
                      Padding(
                        padding: const EdgeInsets.only(top: 4),
                        child: Text(
                          'Connecting to Master -- this can take up to about 15 seconds. This screen updates automatically.',
                          style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
                        ),
                      ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 24),

            Text('Link to Master', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            TextField(
              controller: _masterIdController,
              textCapitalization: TextCapitalization.characters,
              maxLength: 8,
              onTap: () => _editing = true,
              decoration: const InputDecoration(
                labelText: 'Master ID (8 characters)',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 8),
            FilledButton(
              onPressed: _busy ? null : _save,
              child: _busy
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    )
                  : const Text('Save'),
            ),
          ],
        ],
      ),
    );
  }
}
