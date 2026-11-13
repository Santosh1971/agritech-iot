import 'package:flutter/material.dart';

import 'screens/home_screen.dart';
import 'services/house_store.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final store = HouseStore();
  await store.load();
  runApp(AsacFg1App(store: store));
}

class AsacFg1App extends StatelessWidget {
  const AsacFg1App({super.key, required this.store});
  final HouseStore store;

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF2E9C7E); // ASAC teal
    return MaterialApp(
      title: 'ASAC TH',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(colorSchemeSeed: seed, useMaterial3: true),
      darkTheme: ThemeData(colorSchemeSeed: seed, brightness: Brightness.dark, useMaterial3: true),
      home: HomeScreen(store: store),
    );
  }
}
