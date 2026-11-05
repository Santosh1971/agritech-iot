import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'common/demo.dart';
import 'fg1/fg1_app.dart';
import 'wm1/wm1_app.dart';

void main() {
  runApp(const ProviderScope(child: AsacDemoApp()));
}

class AsacDemoApp extends ConsumerWidget {
  const AsacDemoApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final themeMode = ref.watch(themeModeProvider);
    ThemeData theme(Brightness b) => ThemeData(
          colorScheme: ColorScheme.fromSeed(seedColor: asacSeed, brightness: b),
          useMaterial3: true,
          scaffoldBackgroundColor: b == Brightness.light ? const Color(0xFFF4F7F6) : const Color(0xFF121212),
          cardColor: b == Brightness.light ? Colors.white : const Color(0xFF1E1E1E),
          cardTheme: const CardThemeData(
            elevation: 1,
            margin: EdgeInsets.symmetric(horizontal: 16, vertical: 6),
          ),
        );
    return MaterialApp(
      title: 'ASAC Demo',
      debugShowCheckedModeBanner: false,
      themeMode: themeMode,
      theme: theme(Brightness.light),
      darkTheme: theme(Brightness.dark),
      // The DEMO strip sits above every screen, including dialogs' parent
      // routes, so it's always clear nothing real is being switched.
      builder: (context, child) => Column(children: [
        const DemoStrip(),
        Expanded(
          child: MediaQuery.removePadding(
            context: context,
            removeTop: true,
            child: child ?? const SizedBox(),
          ),
        ),
      ]),
      home: const ProductPicker(),
    );
  }
}

class _Product {
  final String code;
  final String name;
  final String tagline;
  final IconData icon;
  final Widget Function()? open;
  const _Product(this.code, this.name, this.tagline, this.icon, this.open);
}

final _products = <_Product>[
  _Product('FG1', 'FlowGuard', 'Pump timer + flow meter. Run by litres or time, daily schedules, history.',
      Icons.water_drop, () => const Fg1DemoShell()),
  _Product('WM1-Mini', 'WaterMaster Mini', 'Pump, fertilizer doser and 4 valves. Multi-zone programs, pressure and flow.',
      Icons.account_tree, () => const Wm1DemoShell()),
  const _Product('WPC', 'Wireless Pump Controller', 'LoRa control of distant pumps.', Icons.settings_input_antenna, null),
  const _Product('WM2', 'WaterMaster 2', 'Farm-wide valves with flow sensing.', Icons.grass, null),
];

class ProductPicker extends StatelessWidget {
  const ProductPicker({super.key});

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Scaffold(
      body: SafeArea(
        top: false,
        child: ListView(padding: const EdgeInsets.fromLTRB(16, 24, 16, 24), children: [
          Center(child: Image.asset('assets/images/logo-full.png', width: 220)),
          const SizedBox(height: 16),
          Text('Try our controllers',
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.bold)),
          const SizedBox(height: 6),
          Text('No hardware needed. Switch pumps and valves, set schedules and watch water flow — '
              'exactly like the real app, on a simulated controller.',
              textAlign: TextAlign.center, style: TextStyle(color: cs.onSurfaceVariant)),
          const SizedBox(height: 20),
          for (final p in _products) _ProductCard(p),
          const SizedBox(height: 16),
          Text('Agri Sensors and Controls',
              textAlign: TextAlign.center, style: TextStyle(color: cs.onSurfaceVariant, fontSize: 12)),
        ]),
      ),
    );
  }
}

class _ProductCard extends StatelessWidget {
  final _Product p;
  const _ProductCard(this.p);

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final enabled = p.open != null;
    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: enabled
            ? () => Navigator.push(context, MaterialPageRoute(builder: (_) => p.open!()))
            : null,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Row(children: [
            Container(
              width: 52,
              height: 52,
              decoration: BoxDecoration(
                color: enabled ? cs.primaryContainer : cs.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(14),
              ),
              child: Icon(p.icon, color: enabled ? cs.onPrimaryContainer : cs.outline, size: 28),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Opacity(
                opacity: enabled ? 1 : 0.55,
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  Row(children: [
                    Text('ASAC ${p.code}',
                        style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
                    const SizedBox(width: 8),
                    if (!enabled)
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                        decoration: BoxDecoration(
                            color: cs.surfaceContainerHighest,
                            borderRadius: BorderRadius.circular(6)),
                        child: const Text('Coming soon', style: TextStyle(fontSize: 10)),
                      ),
                  ]),
                  Text(p.name, style: TextStyle(color: cs.primary, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 4),
                  Text(p.tagline, style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant)),
                ]),
              ),
            ),
            if (enabled) Icon(Icons.chevron_right, color: cs.outline),
          ]),
        ),
      ),
    );
  }
}
