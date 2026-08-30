import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'providers/providers.dart';
import 'screens/main_shell.dart';

void main() {
  runApp(const ProviderScope(child: WM1App()));
}

class WM1App extends ConsumerWidget {
  const WM1App({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeMode = ref.watch(themeModeProvider);
    const seed = Color(0xFF2E7D32); // NB Agri green

    return MaterialApp(
      title: 'NB Agri-WM',
      debugShowCheckedModeBanner: false,
      themeMode: themeMode,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: seed, brightness: Brightness.light),
        useMaterial3: true,
        scaffoldBackgroundColor: const Color(0xFFF5F7F5),
        cardColor: Colors.white,
      ),
      darkTheme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: seed, brightness: Brightness.dark),
        useMaterial3: true,
        cardColor: const Color(0xFF1E1E1E),
      ),
      home: const MainShell(),
    );
  }
}
