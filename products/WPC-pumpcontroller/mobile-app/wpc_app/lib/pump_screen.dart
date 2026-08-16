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

  final _pumpIdController = TextEditingController();
  final _masterIdController = TextEditingController();

  @override
  void initState() {
    super.initState();
    _fetch();
  }

  @override
  void dispose() {
    _pumpIdController.dispose();
    _masterIdController.dispose();
    super.dispose();
  }

  Future<void> _fetch() async {
    setState(() => _busy = true);
    try {
      final info = await WpcApi.getPumpInfo();
      if (!mounted) return;
      setState(() {
        _info = info;
        _error = null;
        _pumpIdController.text = info['pumpId'].toString();
        _masterIdController.text = info['targetMasterId'] as String? ?? '';
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
    final pumpId = int.tryParse(_pumpIdController.text.trim());
    final masterId = _masterIdController.text.trim();
    if (pumpId == null || pumpId < 0 || pumpId > 9999) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Pump ID must be 0-9999')),
      );
      return;
    }
    if (!masterId.toLowerCase().startsWith('0x') || masterId.length < 3) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Master ID should look like 0x86470968')),
      );
      return;
    }
    setState(() => _busy = true);
    try {
      await WpcApi.setPumpConfig(pumpId: pumpId, targetMasterId: masterId);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Saved -- Pump is rejoining under the new settings')),
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
                    Text('Target Master: ${_info!['targetMasterId']}'),
                    Text('Joined: ${_info!['joined'] == true ? 'Yes' : 'No'}'),
                    if (_info!['joined'] == true)
                      Text('Assigned Slot: ${_info!['assignedSlot']}'),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 24),

            Text('Change Settings', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            TextField(
              controller: _pumpIdController,
              keyboardType: TextInputType.number,
              decoration: const InputDecoration(
                labelText: 'Pump ID (0-9999)',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _masterIdController,
              decoration: const InputDecoration(
                labelText: 'Target Master ID (e.g. 0x86470968)',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 16),
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
