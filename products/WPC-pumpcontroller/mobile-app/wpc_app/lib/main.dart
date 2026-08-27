import 'package:flutter/material.dart';
import 'status_screen.dart';
import 'assign_screen.dart';
import 'pump_screen.dart';

void main() => runApp(const WpcApp());

class WpcApp extends StatelessWidget {
  const WpcApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'NB Agri Automation - WPC',
      theme: ThemeData(colorSchemeSeed: Colors.teal, useMaterial3: true),
      home: const HomeShell(),
    );
  }
}

class HomeShell extends StatefulWidget {
  const HomeShell({super.key});

  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int _index = 0;

  static const _screens = [StatusScreen(), AssignScreen(), PumpScreen()];
  static const _titles = ['WPC Status', 'Assign Levels', 'Provision Pump'];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Row(
          children: [
            Image.asset('assets/images/logo_icon.png', height: 28),
            const SizedBox(width: 8),
            Text(_titles[_index]),
          ],
        ),
      ),
      body: _screens[_index],
      bottomNavigationBar: NavigationBar(
        selectedIndex: _index,
        onDestinationSelected: (i) => setState(() => _index = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.dashboard), label: 'Status'),
          NavigationDestination(icon: Icon(Icons.tune), label: 'Assign'),
          NavigationDestination(icon: Icon(Icons.settings_input_antenna), label: 'Provision'),
        ],
      ),
    );
  }
}
