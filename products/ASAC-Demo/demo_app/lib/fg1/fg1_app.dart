import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'providers/providers.dart';
import 'screens/dashboard_screen.dart';
import 'screens/cycles_screen.dart';
import 'screens/manual_screen.dart';
import 'screens/history_screen.dart';
import 'screens/settings_screen.dart';

/// FG1 FlowGuard demo — the real FG1 app's five tabs, driven by the
/// simulated controller.
class Fg1DemoShell extends ConsumerStatefulWidget {
  const Fg1DemoShell({super.key});
  @override
  ConsumerState<Fg1DemoShell> createState() => _Fg1DemoShellState();
}

class _Fg1DemoShellState extends ConsumerState<Fg1DemoShell> {
  int _index = 0;
  static const _screens = [
    DashboardScreen(),
    CyclesScreen(),
    ManualScreen(),
    HistoryScreen(),
    SettingsScreen(),
  ];

  @override
  void initState() {
    super.initState();
    ref.read(deviceServiceProvider).connect();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: IndexedStack(index: _index, children: _screens),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: (i) => setState(() => _index = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.dashboard), label: 'Dashboard'),
          NavigationDestination(icon: Icon(Icons.loop), label: 'Cycles'),
          NavigationDestination(icon: Icon(Icons.pan_tool), label: 'Manual'),
          NavigationDestination(icon: Icon(Icons.history), label: 'History'),
          NavigationDestination(icon: Icon(Icons.settings), label: 'Settings'),
        ],
      ),
    );
  }
}
