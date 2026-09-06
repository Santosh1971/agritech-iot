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
  double? _txPowerSliderValue;   // local while dragging; null = show the server's current value

  @override
  void initState() {
    super.initState();
    _fetch();
    // 1s, not 3s -- this is a direct WiFi connection to the Pump's own
    // SoftAP (no LoRa airtime involved), and fast feedback matters here
    // since this screen is used for live field calibration against IN1/IN4.
    _timer = Timer.periodic(const Duration(seconds: 1), (_) => _fetch());
  }

  @override
  void dispose() {
    _timer?.cancel();
    _masterIdController.dispose();
    super.dispose();
  }

  // Deliberately doesn't touch _busy -- this runs on every 1s auto-refresh
  // tick as well as explicit actions, and _busy also gates the Save button
  // and TX power slider. Toggling it here made those flicker enabled/disabled
  // once a second. Callers that need a "saving" state (_save(), the TX
  // slider's onChangeEnd) set _busy themselves around their own await calls.
  Future<void> _fetch() async {
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
      setState(() => _error = "Not reachable: $e");
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
                    Text('Relay: ${_info!['relay'] == true ? 'ON' : 'OFF'}'),
                    if (_info!['in1Adc'] != null && _info!['in4Adc'] != null)
                      Padding(
                        padding: const EdgeInsets.only(top: 4),
                        child: Text(
                          // Raw ADC counts (0-4095) -- calibration pending.
                          'IN1 raw: ${_info!['in1Adc']}   IN4 raw: ${_info!['in4Adc']}',
                          style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
                        ),
                      ),
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
            const SizedBox(height: 24),

            Text('Pump Radio TX Power', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 2),
            Text(
              // Only affects what THIS Pump transmits -- the Master's own TX
              // power (Status screen, while connected to the Master) needs
              // raising too for range to change in both directions.
              'Higher = longer range, more airtime/battery use. Set the Master separately.',
              style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
            ),
            const SizedBox(height: 4),
            Builder(builder: (context) {
              final serverDbm = ((_info!['txPower'] as num?)?.toDouble() ?? 14).clamp(-9.0, 22.0);
              final displayDbm = _txPowerSliderValue ?? serverDbm;
              return Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text('${displayDbm.round()} dBm', style: Theme.of(context).textTheme.bodyMedium),
                  Slider(
                    min: -9,
                    max: 22,
                    divisions: 31,
                    value: displayDbm,
                    onChanged: _busy
                        ? null
                        : (v) => setState(() => _txPowerSliderValue = v),
                    onChangeEnd: _busy
                        ? null
                        : (v) async {
                            setState(() => _busy = true);
                            try {
                              await WpcApi.setTxPower(v.round());
                              await _fetch();
                            } catch (e) {
                              if (mounted) {
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(content: Text('Failed to set TX power: $e')),
                                );
                              }
                            } finally {
                              if (mounted) {
                                setState(() {
                                  _busy = false;
                                  _txPowerSliderValue = null;
                                });
                              }
                            }
                          },
                  ),
                ],
              );
            }),
          ],
        ],
      ),
    );
  }
}
