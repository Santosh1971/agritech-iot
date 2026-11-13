import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';
import 'log_tab.dart';
import 'sensors_tab.dart';
import 'settings_tab.dart';
import 'status_tab.dart';

class HouseScreen extends StatefulWidget {
  const HouseScreen({super.key, required this.store, required this.house});
  final HouseStore store;
  final House house;
  @override
  State<HouseScreen> createState() => _HouseScreenState();
}

class _HouseScreenState extends State<HouseScreen> {
  int _tab = 0;

  @override
  Widget build(BuildContext context) {
    final c = widget.store.conn(widget.house);
    return ListenableBuilder(
      listenable: c,
      builder: (context, _) {
        final s = c.status == null ? null : St(c.status!);
        final tabs = [
          StatusTab(conn: c),
          SensorsTab(conn: c),
          SettingsTab(store: widget.store, conn: c),
          LogTab(conn: c),
        ];
        return Scaffold(
          appBar: AppBar(
            title: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(widget.house.name),
              if (s != null)
                Text('${s.deviceId}  ·  ${s.time}',
                    style: TextStyle(fontSize: 12, color: Theme.of(context).colorScheme.onSurfaceVariant)),
            ]),
            actions: [Padding(padding: const EdgeInsets.only(right: 8), child: LinkChip(via: c.via, fresh: c.fresh))],
          ),
          body: Column(children: [
            if (s != null && !c.fresh)
              MaterialBanner(
                content: const Text('Controller not reachable — showing last known values'),
                leading: const Icon(Icons.cloud_off),
                actions: [const SizedBox()],
              ),
            Expanded(
              child: s == null
                  ? const Center(
                      child: Column(mainAxisSize: MainAxisSize.min, children: [
                      CircularProgressIndicator(),
                      SizedBox(height: 16),
                      Text('Connecting to the controller…'),
                      SizedBox(height: 4),
                      Text('On site: join its WiFi FG1P_xxxx', style: TextStyle(fontSize: 12)),
                    ]))
                  : tabs[_tab],
            ),
          ]),
          bottomNavigationBar: NavigationBar(
            selectedIndex: _tab,
            onDestinationSelected: (i) => setState(() => _tab = i),
            destinations: const [
              NavigationDestination(icon: Icon(Icons.water_drop_outlined), selectedIcon: Icon(Icons.water_drop), label: 'Status'),
              NavigationDestination(icon: Icon(Icons.sensors_outlined), selectedIcon: Icon(Icons.sensors), label: 'Sensors'),
              NavigationDestination(icon: Icon(Icons.tune_outlined), selectedIcon: Icon(Icons.tune), label: 'Settings'),
              NavigationDestination(icon: Icon(Icons.show_chart), label: 'Log'),
            ],
          ),
        );
      },
    );
  }
}
