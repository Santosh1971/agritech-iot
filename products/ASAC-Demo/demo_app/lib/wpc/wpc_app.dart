import 'package:flutter/material.dart';
import '../common/demo.dart';
import 'api.dart';
import 'assign_screen.dart';
import 'backend.dart';
import 'pump_screen.dart';
import 'status_screen.dart';

/// WPC Wireless Pump Controller demo — the real app's Status / Assign /
/// Provision screens on a simulated Master, plus a Farm tab that shows the
/// sump tank and lets the customer change conditions.
class WpcDemoShell extends StatefulWidget {
  const WpcDemoShell({super.key});
  @override
  State<WpcDemoShell> createState() => _WpcDemoShellState();
}

class _WpcDemoShellState extends State<WpcDemoShell> {
  int _index = 0;
  static const _titles = ['ASAC WPC', 'Assign Levels', 'Pair a new pump', 'Farm & demo'];

  @override
  void initState() {
    super.initState();
    WpcSim.instance.start();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: AsacTitle(_titles[_index])),
      body: IndexedStack(index: _index, children: const [
        _SumpStrip(child: StatusScreen()),
        AssignScreen(),
        _ProvisionHelp(child: PumpScreen()),
        _FarmTab(),
      ]),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: (i) => setState(() => _index = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.dashboard), label: 'Status'),
          NavigationDestination(icon: Icon(Icons.tune), label: 'Assign'),
          NavigationDestination(icon: Icon(Icons.settings_input_antenna), label: 'Provision'),
          NavigationDestination(icon: Icon(Icons.water), label: 'Farm'),
        ],
      ),
    );
  }
}

/// Small live sump gauge above the real Status screen.
class _SumpStrip extends StatelessWidget {
  final Widget child;
  const _SumpStrip({required this.child});
  @override
  Widget build(BuildContext context) => Column(children: [
        ListenableBuilder(
          listenable: WpcSim.instance,
          builder: (context, _) {
            final s = WpcSim.instance;
            final running = s.pumps.where((p) => p.relay).length;
            return Padding(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
              child: Row(children: [
                const Icon(Icons.water, color: Colors.blue),
                const SizedBox(width: 8),
                Expanded(
                  child: ClipRRect(
                    borderRadius: BorderRadius.circular(6),
                    child: LinearProgressIndicator(
                      value: s.sump / 100,
                      minHeight: 14,
                      backgroundColor: Colors.blue.shade50,
                      color: Colors.blue,
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Text('Sump ${s.sump.round()}% · $running running',
                    style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600)),
              ]),
            );
          },
        ),
        Expanded(child: child),
      ]);
}

class _ProvisionHelp extends StatelessWidget {
  final Widget child;
  const _ProvisionHelp({required this.child});
  @override
  Widget build(BuildContext context) => Column(children: [
        Container(
          width: double.infinity,
          margin: const EdgeInsets.fromLTRB(16, 12, 16, 0),
          padding: const EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.secondaryContainer,
            borderRadius: BorderRadius.circular(10),
          ),
          child: const Text(
            'Pretend you are standing at a new pump and your phone is on its WiFi. '
            'Enter the Master ID ${WpcSim.masterId} and Save: the pump joins over '
            'LoRa and appears on the Status screen. Then give it a level on Assign.',
            style: TextStyle(fontSize: 12),
          ),
        ),
        Expanded(child: child),
      ]);
}

class _FarmTab extends StatelessWidget {
  const _FarmTab();

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: Listenable.merge([WpcSim.instance, Backend.instance]),
      builder: (context, _) {
        final s = WpcSim.instance;
        final b = Backend.instance;
        return ListView(padding: const EdgeInsets.symmetric(vertical: 8), children: [
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Text('Sump tank', style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                const SizedBox(height: 4),
                Text('Float switches at each level tell the Master how full it is. '
                    'Pumps far away in the field start and stop by radio to keep it full.',
                    style: TextStyle(fontSize: 12, color: Colors.grey.shade600)),
                const SizedBox(height: 12),
                Center(child: _Tank(sump: s.sump, levels: s.numLevels, threshold: s.threshold)),
                const SizedBox(height: 16),
                const Text('Farm water use'),
                const SizedBox(height: 6),
                SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 0, label: Text('Low')),
                    ButtonSegment(value: 1, label: Text('Normal')),
                    ButtonSegment(value: 2, label: Text('High')),
                  ],
                  selected: {s.usage},
                  onSelectionChanged: (v) {
                    s.usage = v.first;
                    s.changed();
                  },
                ),
              ]),
            ),
          ),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Text('Try what happens if…',
                    style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                const SizedBox(height: 4),
                Text('Cut power at a pump: the app shows "No power" and warns that '
                    'water is not flowing even though the pump was told to run.',
                    style: TextStyle(fontSize: 12, color: Colors.grey.shade600)),
                for (final p in s.pumps)
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    title: Text('Power cut at ${p.name}'),
                    value: !p.powerOk,
                    onChanged: (v) {
                      p.powerOk = !v;
                      p.flowAt = 0;
                      s.changed();
                    },
                  ),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('Power cut at the Master'),
                  value: !s.masterPowerOk,
                  onChanged: (v) {
                    s.masterPowerOk = !v;
                    s.changed();
                  },
                ),
              ]),
            ),
          ),
          Card(
            child: Column(children: [
              SwitchListTile(
                title: const Text('Show power status'),
                value: b.showPowerStatus,
                onChanged: b.setShowPowerStatus,
              ),
              SwitchListTile(
                title: const Text('Show water-flow confirmation'),
                value: b.showWaterFlow,
                onChanged: b.setShowWaterFlow,
              ),
            ]),
          ),
          DemoSettingsCard(productName: 'WPC', onReset: s.reset),
        ]);
      },
    );
  }
}

class _Tank extends StatelessWidget {
  final double sump;
  final int levels;
  final double Function(int) threshold;
  const _Tank({required this.sump, required this.levels, required this.threshold});

  @override
  Widget build(BuildContext context) {
    const h = 180.0, w = 140.0;
    return SizedBox(
      width: w + 70,
      height: h,
      child: Stack(children: [
        Positioned(
          left: 0,
          top: 0,
          child: Container(
            width: w,
            height: h,
            decoration: BoxDecoration(
              border: Border.all(color: Colors.grey.shade500, width: 2),
              borderRadius: const BorderRadius.vertical(bottom: Radius.circular(12)),
            ),
          ),
        ),
        Positioned(
          left: 2,
          bottom: 2,
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 900),
            width: w - 4,
            height: (h - 4) * sump / 100,
            decoration: BoxDecoration(
              color: Colors.blue.shade300,
              borderRadius: const BorderRadius.vertical(bottom: Radius.circular(10)),
            ),
          ),
        ),
        for (var l = 1; l <= levels; l++)
          Positioned(
            left: 0,
            bottom: h * threshold(l) / 100,
            child: Row(children: [
              Container(width: w, height: 1, color: Colors.black45),
              const SizedBox(width: 6),
              Text('Level $l', style: const TextStyle(fontSize: 11)),
            ]),
          ),
        Positioned(
          left: 0,
          top: h / 2 - 10,
          width: w,
          child: Text('${sump.round()}%',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
        ),
      ]),
    );
  }
}
