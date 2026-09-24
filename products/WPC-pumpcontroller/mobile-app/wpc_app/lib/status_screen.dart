import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'api.dart';
import 'backend.dart';

class StatusScreen extends StatefulWidget {
  const StatusScreen({super.key});

  @override
  State<StatusScreen> createState() => _StatusScreenState();
}

class _StatusScreenState extends State<StatusScreen> {
  static const Duration _pollInterval = Duration(seconds: 3);

  Map<String, dynamic>? _status;
  String? _error;
  Timer? _timer;
  bool _busy = false;

  @override
  void initState() {
    super.initState();
    _fetch();
    _timer = Timer.periodic(_pollInterval, (_) => _fetch());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  Future<void> _fetch() async {
    try {
      final status = await WpcApi.getStatus();
      if (!mounted) return;
      setState(() {
        _status = status;
        _error = null;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() => _error = 'Not reachable: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    // Rebuilds on a Dashboard-display toggle (Connection screen) even though this
    // screen's own data comes from the separate _fetch()/Timer polling above.
    return ListenableBuilder(
      listenable: Backend.instance,
      builder: (context, _) => RefreshIndicator(onRefresh: _fetch, child: _buildBody()),
    );
  }

  Widget _buildBody() {
    if (_error != null) {
      return ListView(
        children: [
          Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              children: [
                const Icon(Icons.wifi_off, size: 48, color: Colors.grey),
                const SizedBox(height: 12),
                Text(_error!, textAlign: TextAlign.center),
              ],
            ),
          ),
        ],
      );
    }

    if (_status == null) {
      return const Center(child: CircularProgressIndicator());
    }

    final masterId = (_status!['masterId'] as String? ?? '--')
        .replaceFirst(RegExp(r'^0x', caseSensitive: false), '');
    final numLevels = (_status!['numLevels'] as num?)?.toInt() ?? 0;
    final levels = (_status!['levels'] as List<dynamic>? ?? []);
    // powerOk is the corrected-polarity field (true = power present); noPower is kept only for
    // an old cached status that predates it. Gated on the Dashboard-display toggle either way.
    final powerOk = _status!.containsKey('powerOk') ? _status!['powerOk'] == true : _status!['noPower'] != true;
    final showNoPowerBanner = Backend.instance.showPowerStatus && !powerOk;
    final pumps = _status!['pumps'] as List<dynamic>? ?? [];
    // Cloud mode only: the broker keeps the Master's last status, which can be
    // hours old if the Master lost its internet -- never let that look live.
    final masterOffline = _status!['_masterOnline'] == false;
    final wifi = (_status!['wifi'] as Map<String, dynamic>?) ?? {};
    final fw = _status!['fw'] as String?;

    final unassigned = pumps.where((p) {
      final assigned = ((p as Map<String, dynamic>)['assignedLevels'] as List<dynamic>? ?? []);
      return assigned.isEmpty;
    }).toList();

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Row(
          children: [
            Text('Master: $masterId', style: Theme.of(context).textTheme.bodySmall),
            const SizedBox(width: 4),
            InkWell(
              onTap: () {
                Clipboard.setData(ClipboardData(text: masterId));
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('Master ID copied')),
                );
              },
              child: const Icon(Icons.copy, size: 16, color: Colors.grey),
            ),
          ],
        ),
        if (fw != null || wifi['configured'] == true)
          Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Text(
              [
                if (fw != null) 'Firmware $fw',
                if (wifi['configured'] == true)
                  wifi['connected'] == true
                      ? 'Internet: connected'
                      : 'Internet: not connected',
              ].join('   '),
              style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
            ),
          ),
        const SizedBox(height: 12),

        if (masterOffline)
          Container(
            padding: const EdgeInsets.all(12),
            margin: const EdgeInsets.only(bottom: 16),
            decoration: BoxDecoration(
              color: Colors.orange.shade50,
              border: Border.all(color: Colors.orange.shade300),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Row(
              children: [
                Icon(Icons.cloud_off, color: Colors.orange.shade800),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    'This Master is offline. What you see below is its last known state, not live.',
                    style: TextStyle(color: Colors.orange.shade900, fontWeight: FontWeight.w500),
                  ),
                ),
              ],
            ),
          ),

        if (showNoPowerBanner)
          Container(
            padding: const EdgeInsets.all(12),
            margin: const EdgeInsets.only(bottom: 16),
            decoration: BoxDecoration(
              color: Colors.red.shade50,
              border: Border.all(color: Colors.red.shade300),
              borderRadius: BorderRadius.circular(8),
            ),
            child: Row(
              children: [
                Icon(Icons.power_off, color: Colors.red.shade700),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    'No Power detected',
                    style: TextStyle(color: Colors.red.shade700, fontWeight: FontWeight.bold),
                  ),
                ),
              ],
            ),
          ),

        // Levels top-to-bottom as Level N .. Level 1, matching the physical
        // board layout (highest level at top). Each box shows the level's
        // own state plus the pumps assigned to it -- no slot numbers, since
        // that's internal wire-protocol addressing, not something an
        // installer needs to see.
        for (int lvl = numLevels; lvl >= 1; lvl--) ...[
          _buildLevelBox(context, lvl, levels, pumps),
          const SizedBox(height: 12),
        ],

        if (unassigned.isNotEmpty) ...[
          Text('Unassigned', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          Container(
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: Colors.grey.shade100,
              border: Border.all(color: Colors.grey.shade300),
              borderRadius: BorderRadius.circular(10),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: unassigned.map((p) => _pumpRow(p as Map<String, dynamic>)).toList(),
            ),
          ),
        ],
      ],
    );
  }

  Future<void> _setOverride(int slot, bool enabled, {bool? state}) async {
    setState(() => _busy = true);
    try {
      await WpcApi.setPumpOverride(slot, enabled, state: state);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to set override: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Widget _buildLevelBox(
      BuildContext context, int lvl, List<dynamic> levels, List<dynamic> pumps) {
    final active = (lvl - 1) < levels.length && levels[lvl - 1] == true;
    final levelPumps = pumps.where((p) {
      final assigned = ((p as Map<String, dynamic>)['assignedLevels'] as List<dynamic>? ?? [])
          .map((e) => (e as num).toInt());
      return assigned.contains(lvl);
    }).toList();

    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: active ? Colors.amber.shade50 : Colors.grey.shade100,
        border: Border.all(color: active ? Colors.amber.shade300 : Colors.grey.shade300),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                active ? Icons.water_drop : Icons.water_drop_outlined,
                color: active ? Colors.amber.shade800 : Colors.grey,
              ),
              const SizedBox(width: 8),
              Text('Level $lvl', style: Theme.of(context).textTheme.titleMedium),
            ],
          ),
          const SizedBox(height: 8),
          if (levelPumps.isEmpty)
            const Text('No pumps assigned', style: TextStyle(color: Colors.grey)),
          ...levelPumps.map((p) => _pumpRow(p as Map<String, dynamic>)),
        ],
      ),
    );
  }

  Future<void> _forgetPump(int slot, String displayName) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Unpair this pump?'),
        content: Text(
          '$displayName will be removed from this Master and no longer show anywhere. '
          "This doesn't affect the physical Pump Node -- it still has this Master saved and will "
          'rejoin on its own (e.g. after a restart) unless you also connect to that Pump\'s own '
          'WiFi and use Provision → "Forget this Master".',
        ),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Unpair', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    setState(() => _busy = true);
    try {
      await WpcApi.forgetPump(slot);
      await _fetch();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to unpair: $e')),
        );
      }
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Widget _pumpRow(Map<String, dynamic> map) {
    final online = map['online'] == true;
    final relay = map['relay'] == true;
    final name = (map['name'] as String?) ?? '';
    final displayName = name.isNotEmpty ? name : 'Pump ${map['pumpId']}';
    final slot = (map['slot'] as num).toInt();
    final in1Adc = (map['in1Adc'] as num?)?.toInt();
    final in4Adc = (map['in4Adc'] as num?)?.toInt();
    final powerOk = map['powerOk'] == true;
    final waterFlow = map['waterFlow'] == true;
    final override = map['override'] as Map<String, dynamic>? ?? {};
    final overrideEnabled = override['enabled'] == true;
    final overrideState = override['state'] == true;
    final b = Backend.instance;

    return Padding(
      padding: const EdgeInsets.only(top: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(
                relay ? Icons.power : Icons.power_off,
                size: 18,
                color: !online ? Colors.red.shade300 : (relay ? Colors.green : Colors.grey),
              ),
              const SizedBox(width: 6),
              Expanded(child: Text(displayName)),
              if (overrideEnabled)
                Padding(
                  padding: const EdgeInsets.only(right: 6),
                  child: Icon(Icons.pan_tool, size: 14, color: Colors.orange.shade700),
                ),
              Text(
                !online ? 'Offline' : (relay ? 'Running' : 'Idle'),
                style: TextStyle(
                  color: !online ? Colors.red : (relay ? Colors.green.shade700 : Colors.grey),
                  fontWeight: FontWeight.w500,
                ),
              ),
              PopupMenuButton<String>(
                icon: Icon(Icons.more_vert, size: 18, color: Colors.grey.shade600),
                padding: EdgeInsets.zero,
                onSelected: (v) {
                  if (v == 'unpair') _forgetPump(slot, displayName);
                },
                itemBuilder: (ctx) => const [
                  PopupMenuItem(
                    value: 'unpair',
                    child: Text('Unpair this pump', style: TextStyle(color: Colors.red)),
                  ),
                ],
              ),
            ],
          ),
          if (online && (b.showPowerStatus || b.showWaterFlow))
            Padding(
              padding: const EdgeInsets.only(left: 24, top: 2),
              child: Row(
                children: [
                  if (b.showPowerStatus) ...[
                    Icon(powerOk ? Icons.bolt : Icons.power_off,
                        size: 14, color: powerOk ? Colors.green.shade600 : Colors.red.shade600),
                    const SizedBox(width: 3),
                    Text(powerOk ? 'Power OK' : 'No power',
                        style: TextStyle(
                            fontSize: 11,
                            color: powerOk ? Colors.grey.shade600 : Colors.red.shade600,
                            fontWeight: powerOk ? FontWeight.normal : FontWeight.w600)),
                  ],
                  if (b.showPowerStatus && b.showWaterFlow) const SizedBox(width: 10),
                  if (b.showWaterFlow) ...[
                    Icon(waterFlow ? Icons.water_drop : Icons.water_drop_outlined,
                        size: 14, color: waterFlow ? Colors.blue.shade600 : Colors.grey.shade400),
                    const SizedBox(width: 3),
                    Text(
                      waterFlow ? 'Flow confirmed' : (relay ? 'No flow yet' : 'No flow'),
                      style: TextStyle(
                          fontSize: 11,
                          color: (relay && !waterFlow) ? Colors.orange.shade800 : Colors.grey.shade600,
                          fontWeight: (relay && !waterFlow) ? FontWeight.w600 : FontWeight.normal),
                    ),
                  ],
                ],
              ),
            ),
          if (in1Adc != null && in4Adc != null)
            Padding(
              padding: const EdgeInsets.only(left: 24, top: 2),
              child: Text(
                // Raw ADC counts (0-4095) -- calibration to real units is
                // pending, so shown as-is rather than implying calibrated units.
                'IN1 raw: $in1Adc   IN4 raw: $in4Adc',
                style: TextStyle(fontSize: 11, color: Colors.grey.shade600),
              ),
            ),
          Padding(
            padding: const EdgeInsets.only(left: 24, top: 4),
            child: Row(
              children: [
                Text('Manual', style: TextStyle(fontSize: 12, color: Colors.grey.shade700)),
                Switch(
                  value: overrideEnabled,
                  onChanged: _busy
                      ? null
                      : (v) => _setOverride(slot, v, state: v ? relay : null),
                ),
                if (overrideEnabled) ...[
                  const SizedBox(width: 8),
                  ChoiceChip(
                    label: const Text('ON'),
                    selected: overrideState,
                    onSelected: _busy ? null : (_) => _setOverride(slot, true, state: true),
                  ),
                  const SizedBox(width: 6),
                  ChoiceChip(
                    label: const Text('OFF'),
                    selected: !overrideState,
                    onSelected: _busy ? null : (_) => _setOverride(slot, true, state: false),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}
