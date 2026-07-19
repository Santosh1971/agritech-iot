import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import 'screens/dashboard_screen.dart';
import 'screens/cycles_screen.dart';
import 'screens/manual_screen.dart';
import 'screens/history_screen.dart';
import 'screens/settings_screen.dart';
import 'providers/providers.dart';
void main() {
  runApp(const ProviderScope(child: SWCApp()));
}

class SWCApp extends ConsumerWidget {
  const SWCApp({super.key});
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeMode = ref.watch(themeModeProvider);
    return MaterialApp(
      title: 'SWC',
      debugShowCheckedModeBanner: false,
      themeMode: themeMode,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF1565C0),
          brightness: Brightness.light,
        ),
        scaffoldBackgroundColor: const Color(0xFFF5F6FA),
        cardColor: Colors.white,
        appBarTheme: const AppBarTheme(
          backgroundColor: Colors.white,
          foregroundColor: Colors.black87,
          elevation: 0,
        ),
        useMaterial3: true,
        cardTheme: const CardThemeData(
          elevation: 2,
          margin: EdgeInsets.symmetric(horizontal: 16, vertical: 6),
        ),
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF1565C0),
          brightness: Brightness.dark,
        ),
        scaffoldBackgroundColor: const Color(0xFF121212),
        // Real dark surface for cards now — every card/container
        // background across the app reads Theme.of(context).cardColor
        // instead of a hardcoded white, and text reads
        // Theme.of(context).colorScheme.onSurface/onSurfaceVariant
        // instead of a hardcoded dark color, so both sides of the
        // contrast now actually adapt together.
        cardColor: const Color(0xFF1E1E1E),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF1E1E1E),
          foregroundColor: Colors.white,
          elevation: 0,
        ),
        useMaterial3: true,
        cardTheme: const CardThemeData(
          elevation: 2,
          margin: EdgeInsets.symmetric(horizontal: 16, vertical: 6),
        ),
      ),
      home: const MainShell(),
    );
  }
}
class MainShell extends ConsumerStatefulWidget {
  const MainShell({super.key});
  @override
  ConsumerState<MainShell> createState() => _MainShellState();
}
class _MainShellState extends ConsumerState<MainShell> {
  int _currentIndex = 0;
  final _screens = const [
    DashboardScreen(),
    CyclesScreen(),
    ManualScreen(),
    HistoryScreen(),
    SettingsScreen(),
  ];
  @override
  void initState() {
    super.initState();
    // Keep screen awake while app is in foreground — testing/monitoring
    // an active irrigation cycle shouldn't be interrupted by screen sleep.
    WakelockPlus.enable();
    // Auto-connect to the active transport (local SoftAP by default,
    // or cloud/MQTT if the user switched modes in Settings) on startup
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      final device = ref.read(deviceServiceProvider);
      await device.connect();
    });
  }
  @override
  void dispose() {
    WakelockPlus.disable();
    super.dispose();
  }
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      // IndexedStack keeps all screens mounted (just toggles visibility)
      // instead of destroying/recreating them on every tab switch. That
      // matters a lot here: DashboardScreen's initState() opens a
      // connection — with the old body: _screens[_currentIndex] approach,
      // navigating away and back tore down and reopened the WebSocket
      // every single time, which is why the local (SoftAP) transport in
      // particular showed flickering "Offline" status even though it was
      // actually reconnecting fine each time.
      body: IndexedStack(index: _currentIndex, children: _screens),
      bottomNavigationBar: NavigationBar(
        selectedIndex: _currentIndex,
        onDestinationSelected: (i) => setState(() => _currentIndex = i),
        destinations: const [
          NavigationDestination(icon: Icon(Icons.dashboard), label: 'Dashboard'),
          NavigationDestination(icon: Icon(Icons.loop),      label: 'Cycles'),
          NavigationDestination(icon: Icon(Icons.pan_tool),  label: 'Manual'),
          NavigationDestination(icon: Icon(Icons.history),   label: 'History'),
          NavigationDestination(icon: Icon(Icons.settings),  label: 'Settings'),
        ],
      ),
    );
  }
}
