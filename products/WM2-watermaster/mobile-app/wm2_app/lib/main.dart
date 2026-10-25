import 'package:flutter/material.dart';

import 'screens/node_list_screen.dart';
import 'services/node_store.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final store = NodeStore();
  await store.load();
  runApp(Wm2App(store: store));
}

class Wm2App extends StatelessWidget {
  const Wm2App({super.key, required this.store});
  final NodeStore store;

  @override
  Widget build(BuildContext context) {
    const seed = Color(0xFF2E9C7E); // ASAC teal (logo background #57B99C, darker for contrast)
    return MaterialApp(
      title: 'ASAC-WM2',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(colorSchemeSeed: seed, useMaterial3: true),
      darkTheme: ThemeData(colorSchemeSeed: seed, brightness: Brightness.dark, useMaterial3: true),
      home: NodeListScreen(store: store),
    );
  }
}
