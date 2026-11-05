import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'providers/providers.dart';
import 'screens/dashboard_screen.dart';
import 'screens/programs_screen.dart';
import 'screens/manual_control_screen.dart';
import 'screens/settings_screen.dart';

/// WM1-Mini WaterMaster demo — the real app's four tabs, driven by the
/// simulated controller.
class Wm1DemoShell extends ConsumerStatefulWidget {
  const Wm1DemoShell({super.key});
  @override
  ConsumerState<Wm1DemoShell> createState() => _Wm1DemoShellState();
}

class _Wm1DemoShellState extends ConsumerState<Wm1DemoShell> {
  int _index = 0;

  static const _screens = [
    DashboardScreen(),
    ProgramsScreen(),
    ManualControlScreen(),
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
          NavigationDestination(icon: Icon(Icons.dashboard_outlined), selectedIcon: Icon(Icons.dashboard), label: 'Dashboard'),
          NavigationDestination(icon: Icon(Icons.calendar_month_outlined), selectedIcon: Icon(Icons.calendar_month), label: 'Programs'),
          NavigationDestination(icon: Icon(Icons.pan_tool_outlined), selectedIcon: Icon(Icons.pan_tool), label: 'Manual'),
          NavigationDestination(icon: Icon(Icons.settings_outlined), selectedIcon: Icon(Icons.settings), label: 'Settings'),
        ],
      ),
    );
  }
}
