import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../providers/providers.dart';
import '../utils/time_format.dart';
import '../widgets/pump_icon.dart';

class ManualScreen extends ConsumerStatefulWidget {
  const ManualScreen({super.key});
  @override
  ConsumerState<ManualScreen> createState() => _ManualScreenState();
}

class _ManualScreenState extends ConsumerState<ManualScreen> {
  final _litersController = TextEditingController(text: '10.0');

  @override
  void dispose() {
    _litersController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final statusAsync = ref.watch(deviceStatusProvider);
    final mqtt = ref.read(deviceServiceProvider);
    final pumpOn = statusAsync.maybeWhen(
        data: (s) => s.pumpOn, orElse: () => false);

    return Scaffold(
      appBar: AppBar(
        elevation: 0,
        leading: BackButton(color: Theme.of(context).colorScheme.onSurface),
        title: Text('Manual Control',
            style: TextStyle(color: Theme.of(context).colorScheme.onSurface, fontWeight: FontWeight.w600)),
        centerTitle: true,
      ),
      body: ListView(
        padding: const EdgeInsets.all(24),
        children: [
          // Motor image / pump icon
          Container(
            padding: const EdgeInsets.all(24),
            decoration: BoxDecoration(
              color: Theme.of(context).cardColor,
              borderRadius: BorderRadius.circular(16),
              boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
                  blurRadius: 8, offset: const Offset(0, 2))],
            ),
            child: Column(children: [
              PumpIcon(size: 100, isOn: pumpOn,
                  color: pumpOn ? const Color(0xFFFF9800) : Colors.grey.shade400),
              const SizedBox(height: 16),
              Text('PUMP IS',
                  style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 16)),
              Text(pumpOn ? 'ON' : 'OFF',
                  style: TextStyle(
                    fontSize: 32, fontWeight: FontWeight.bold,
                    color: pumpOn ? const Color(0xFF4CAF50) : Colors.grey)),
              if (pumpOn) ...[
                const SizedBox(height: 4),
                statusAsync.maybeWhen(
                  data: (s) => Text('Since ${formatTime12FromString(s.rtcTime)}',
                      style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant)),
                  orElse: () => const SizedBox(),
                ),
              ],
            ]),
          ),
          const SizedBox(height: 24),

          // ON / OFF buttons
          Row(children: [
            Expanded(
              child: ElevatedButton.icon(
                onPressed: pumpOn ? null : () => mqtt.manualOn(),
                icon: const Icon(Icons.play_arrow, color: Colors.white),
                label: const Text('TURN ON',
                    style: TextStyle(color: Colors.white,
                        fontWeight: FontWeight.bold, fontSize: 16)),
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF4CAF50),
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: ElevatedButton.icon(
                onPressed: pumpOn ? () => mqtt.manualOff() : null,
                icon: const Icon(Icons.stop, color: Colors.white),
                label: const Text('TURN OFF',
                    style: TextStyle(color: Colors.white,
                        fontWeight: FontWeight.bold, fontSize: 16)),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.red,
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
              ),
            ),
          ]),
          const SizedBox(height: 24),

          // Manual Water Delivery
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Theme.of(context).cardColor,
              borderRadius: BorderRadius.circular(16),
              boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.05),
                  blurRadius: 8, offset: const Offset(0, 2))],
            ),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Manual Water (Optional)',
                  style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant, fontSize: 13)),
              const SizedBox(height: 8),
              TextField(
                controller: _litersController,
                keyboardType: const TextInputType.numberWithOptions(decimal: true),
                decoration: InputDecoration(
                  suffix: Text('Liters',
                      style: TextStyle(color: Theme.of(context).colorScheme.onSurfaceVariant)),
                  border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(8)),
                  contentPadding: const EdgeInsets.symmetric(
                      horizontal: 12, vertical: 12),
                ),
              ),
              const SizedBox(height: 12),
              SizedBox(
                width: double.infinity,
                child: ElevatedButton(
                  onPressed: () {
                    final liters = double.tryParse(
                        _litersController.text) ?? 0;
                    if (liters > 0) mqtt.manualLiters(liters);
                  },
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF2196F3),
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(12)),
                  ),
                  child: Text(
                    'START FOR ${_litersController.text} LITERS',
                    style: const TextStyle(color: Colors.white,
                        fontWeight: FontWeight.bold, fontSize: 15),
                  ),
                ),
              ),
            ]),
          ),
        ],
      ),
    );
  }
}
