import 'package:flutter/material.dart';

import '../services/farm.dart';
import '../services/node_store.dart';
import 'farm_screen.dart';
import 'node_list_screen.dart';

/// Farm dashboard and node list, with one live link per node shared by both.
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key, required this.store});
  final NodeStore store;
  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  late final FarmController farm = FarmController(widget.store);
  int tab = 0;

  @override
  void dispose() {
    farm.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(index: tab, children: [
        FarmScreen(farm: farm, store: widget.store),
        NodeListScreen(store: widget.store),
      ]),
      bottomNavigationBar: NavigationBar(
        selectedIndex: tab,
        onDestinationSelected: (i) => setState(() => tab = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.grass_outlined), label: 'Farm'),
          NavigationDestination(icon: Icon(Icons.list_alt), label: 'Nodes'),
        ],
      ),
    );
  }
}
