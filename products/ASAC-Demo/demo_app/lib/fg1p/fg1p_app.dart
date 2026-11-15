import 'package:flutter/material.dart';
import 'screens/house_screen.dart';
import 'services/house_store.dart';

/// FG1 Polyhouse misting demo — the real polyhouse app's screens on its
/// built-in simulated controller (temperature and humidity swing over
/// about 10 minutes, so misting starts and stops while you watch).
class Fg1pDemoShell extends StatefulWidget {
  const Fg1pDemoShell({super.key});
  @override
  State<Fg1pDemoShell> createState() => _Fg1pDemoShellState();
}

class _Fg1pDemoShellState extends State<Fg1pDemoShell> {
  static final HouseStore _store = HouseStore();
  static Future<void>? _loading;
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    _loading ??= _store.load().then((_) async {
      if (_store.houses.isEmpty) {
        await _store.add(House(name: 'Rose polyhouse', deviceId: 'FG1P_DEMO', host: '', demo: true));
      }
    });
    _loading!.then((_) {
      if (mounted) setState(() => _ready = true);
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!_ready) return const Scaffold(body: Center(child: CircularProgressIndicator()));
    return ListenableBuilder(
      listenable: _store,
      builder: (context, _) {
        if (_store.houses.isEmpty) {
          // "Remove" in Settings: put the demo house straight back.
          _store.add(House(name: 'Rose polyhouse', deviceId: 'FG1P_DEMO', host: '', demo: true));
          return const Scaffold(body: Center(child: CircularProgressIndicator()));
        }
        return HouseScreen(store: _store, house: _store.houses.first);
      },
    );
  }
}
