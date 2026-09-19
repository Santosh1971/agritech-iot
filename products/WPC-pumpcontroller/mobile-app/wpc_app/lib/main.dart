import 'package:flutter/material.dart';
import 'status_screen.dart';
import 'assign_screen.dart';
import 'pump_screen.dart';
import 'connection_screen.dart';
import 'backend.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Backend.instance.load();
  runApp(const WpcApp());
}

class WpcApp extends StatelessWidget {
  const WpcApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'NB Agri-WPC',
      debugShowCheckedModeBanner: false,
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
            Expanded(child: Text(_titles[_index], overflow: TextOverflow.ellipsis)),
          ],
        ),
        actions: [
          // Shows how we're connected (and to which installation in Cloud
          // mode); tapping opens the Connection screen.
          ListenableBuilder(
            listenable: Backend.instance,
            builder: (context, _) {
              final b = Backend.instance;
              final cloud = b.mode == LinkMode.cloud;
              return TextButton.icon(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute(builder: (_) => const ConnectionScreen()),
                ),
                icon: Icon(cloud ? Icons.cloud_outlined : Icons.wifi, size: 18),
                label: Text(cloud ? (b.active?.label ?? 'Cloud') : 'Local'),
              );
            },
          ),
        ],
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
