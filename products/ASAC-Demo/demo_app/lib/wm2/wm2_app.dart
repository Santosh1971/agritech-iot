import 'package:flutter/material.dart';
import '../common/demo.dart';
import 'screens/farm_screen.dart';
import 'screens/node_list_screen.dart';
import 'services/demo_link.dart';
import 'services/farm.dart';
import 'services/node_store.dart';

/// WM2 WaterMaster demo — the real app's Farm and Nodes screens on four
/// simulated battery-powered valve nodes, plus a Demo tab to turn the
/// farm's water supply on and off.
class Wm2DemoShell extends StatefulWidget {
  const Wm2DemoShell({super.key});
  @override
  State<Wm2DemoShell> createState() => _Wm2DemoShellState();
}

class _Wm2DemoShellState extends State<Wm2DemoShell> {
  static final NodeStore _store = NodeStore();
  static Future<void>? _loading;
  FarmController? _farm;
  int _tab = 0;

  @override
  void initState() {
    super.initState();
    _loading ??= _store.load();
    _loading!.then((_) {
      if (mounted) setState(() => _farm = FarmController(_store));
    });
  }

  @override
  void dispose() {
    _farm?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final farm = _farm;
    if (farm == null) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    return Scaffold(
      body: IndexedStack(index: _tab, children: [
        FarmScreen(farm: farm, store: _store),
        NodeListScreen(store: _store),
        _DemoTab(store: _store, farm: farm),
      ]),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _tab,
        onDestinationSelected: (i) => setState(() => _tab = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.grass_outlined), label: 'Farm'),
          NavigationDestination(icon: Icon(Icons.list_alt), label: 'Nodes'),
          NavigationDestination(icon: Icon(Icons.science_outlined), label: 'Demo'),
        ],
      ),
    );
  }
}

class _DemoTab extends StatelessWidget {
  final NodeStore store;
  final FarmController farm;
  const _DemoTab({required this.store, required this.farm});

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const AsacTitle('Farm & demo')),
      body: ListenableBuilder(
        listenable: Wm2Farm.instance,
        builder: (context, _) {
          final f = Wm2Farm.instance;
          return ListView(padding: const EdgeInsets.symmetric(vertical: 8), children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  const Text('How WM2 works',
                      style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                  const SizedBox(height: 8),
                  const Text(
                      '• Each node is solar/battery powered and drives 2 latching valves.\n'
                      '• A pressure switch tells the node when water is in the pipe.\n'
                      '• On programmed days, a valve opens as soon as water arrives and closes '
                      'when its minutes or litres for the day are done.\n'
                      '• Flow sensors count every litre, per valve.\n'
                      '• If water stops, valves close and carry on when it comes back.'),
                  const SizedBox(height: 12),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    secondary: Icon(Icons.water, color: f.water ? Colors.blue : Colors.grey),
                    title: const Text('Water supply ON'),
                    subtitle: const Text('Main pump / canal water reaching the farm'),
                    value: f.water,
                    onChanged: f.setWater,
                  ),
                ]),
              ),
            ),
            DemoSettingsCard(
              productName: 'WM2',
              onReset: () async {
                Wm2Farm.instance.reset();
                await store.resetDemo();
              },
            ),
          ]);
        },
      ),
    );
  }
}
