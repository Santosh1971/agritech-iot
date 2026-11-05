import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// ASAC brand colour (logo background #57B99C, darkened for contrast) —
/// same seed the ASAC-WM2 app uses.
const asacSeed = Color(0xFF2E9C7E);

/// How fast simulated water flows / time passes inside a running cycle.
/// The phone's real clock still drives the schedule, so a cycle set for
/// 10:30 starts at 10:30 — only the run itself is sped up, so a customer
/// doesn't have to wait 30 minutes to watch a cycle finish.
class DemoSpeed {
  static int value = 10;
  static const choices = [1, 10, 60];
}

const _speedKey = 'demo_speed';
const _themeKey = 'theme_mode';

class DemoSpeedNotifier extends StateNotifier<int> {
  DemoSpeedNotifier() : super(DemoSpeed.value) {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final v = prefs.getInt(_speedKey);
    if (v != null && DemoSpeed.choices.contains(v)) {
      DemoSpeed.value = v;
      state = v;
    }
  }

  Future<void> set(int v) async {
    DemoSpeed.value = v;
    state = v;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt(_speedKey, v);
  }
}

final demoSpeedProvider =
    StateNotifierProvider<DemoSpeedNotifier, int>((ref) => DemoSpeedNotifier());

class ThemeModeNotifier extends StateNotifier<ThemeMode> {
  ThemeModeNotifier() : super(ThemeMode.light) {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    switch (prefs.getString(_themeKey)) {
      case 'dark':
        state = ThemeMode.dark;
      case 'system':
        state = ThemeMode.system;
    }
  }

  Future<void> setMode(ThemeMode mode) async {
    state = mode;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_themeKey, mode.name);
  }
}

final themeModeProvider =
    StateNotifierProvider<ThemeModeNotifier, ThemeMode>((ref) => ThemeModeNotifier());

/// A broadcast stream that hands each new listener the latest value
/// first. The real apps get fresh pushes from the device every second;
/// here a screen opened later must still see the current programs /
/// history without waiting for the next change.
class ReplaySubject<T> {
  final _ctrl = StreamController<T>.broadcast();
  T? _last;
  bool _has = false;

  T? get value => _last;

  void add(T v) {
    _last = v;
    _has = true;
    if (!_ctrl.isClosed) _ctrl.add(v);
  }

  Stream<T> get stream => Stream<T>.multi((c) {
        if (_has) c.add(_last as T);
        final sub = _ctrl.stream.listen(c.add, onError: c.addError);
        c.onCancel = sub.cancel;
      }, isBroadcast: true);

  void close() => _ctrl.close();
}

/// Thin strip shown above every screen so nobody mistakes the demo for a
/// live controller.
class DemoStrip extends StatelessWidget {
  const DemoStrip({super.key});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: const Color(0xFFFFB300),
      child: SafeArea(
        bottom: false,
        child: InkWell(
          onTap: () => showDemoInfo(context),
          child: const Padding(
            padding: EdgeInsets.symmetric(horizontal: 12, vertical: 5),
            child: Row(children: [
              Icon(Icons.science_outlined, size: 16, color: Colors.black87),
              SizedBox(width: 6),
              Expanded(
                child: Text(
                  'DEMO MODE · simulated device, no hardware connected',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                      fontSize: 12, fontWeight: FontWeight.w600, color: Colors.black87),
                ),
              ),
              Icon(Icons.info_outline, size: 16, color: Colors.black87),
            ]),
          ),
        ),
      ),
    );
  }
}

void showDemoInfo(BuildContext context) {
  showDialog<void>(
    context: context,
    builder: (_) => AlertDialog(
      icon: const Icon(Icons.science_outlined),
      title: const Text('This is a demo'),
      content: const Text(
        'Everything you see is simulated on this phone — there is no controller, '
        'pump or sensor connected.\n\n'
        'Switch relays, create schedules and watch the water flow exactly as you '
        'would on the real product. Cycles run faster than real time so you can '
        'see them finish (change the speed in Settings).\n\n'
        'To buy or for a live demonstration, contact Agri Sensors and Controls.',
      ),
      actions: [
        TextButton(onPressed: () => Navigator.pop(context), child: const Text('OK')),
      ],
    ),
  );
}

/// Settings card shared by every product: demo speed, theme, reset.
class DemoSettingsCard extends ConsumerWidget {
  final VoidCallback onReset;
  final String productName;
  const DemoSettingsCard({super.key, required this.onReset, required this.productName});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final speed = ref.watch(demoSpeedProvider);
    final theme = ref.watch(themeModeProvider);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          const Row(children: [
            Icon(Icons.science_outlined),
            SizedBox(width: 8),
            Text('Demo settings', style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
          ]),
          const SizedBox(height: 12),
          const Text('Simulation speed'),
          const SizedBox(height: 6),
          SegmentedButton<int>(
            segments: [
              for (final s in DemoSpeed.choices)
                ButtonSegment(value: s, label: Text(s == 1 ? 'Real time' : '${s}x')),
            ],
            selected: {speed},
            onSelectionChanged: (v) => ref.read(demoSpeedProvider.notifier).set(v.first),
          ),
          const SizedBox(height: 16),
          const Text('Theme'),
          const SizedBox(height: 6),
          SegmentedButton<ThemeMode>(
            segments: const [
              ButtonSegment(value: ThemeMode.light, label: Text('Light')),
              ButtonSegment(value: ThemeMode.dark, label: Text('Dark')),
              ButtonSegment(value: ThemeMode.system, label: Text('Auto')),
            ],
            selected: {theme},
            onSelectionChanged: (v) => ref.read(themeModeProvider.notifier).setMode(v.first),
          ),
          const SizedBox(height: 16),
          SizedBox(
            width: double.infinity,
            child: OutlinedButton.icon(
              icon: const Icon(Icons.restart_alt),
              label: Text('Reset $productName demo'),
              onPressed: () async {
                final ok = await showDialog<bool>(
                  context: context,
                  builder: (_) => AlertDialog(
                    title: const Text('Reset demo?'),
                    content: const Text(
                        'Puts back the sample schedules and history and turns everything off.'),
                    actions: [
                      TextButton(
                          onPressed: () => Navigator.pop(context, false),
                          child: const Text('Cancel')),
                      FilledButton(
                          onPressed: () => Navigator.pop(context, true),
                          child: const Text('Reset')),
                    ],
                  ),
                );
                if (ok == true) {
                  onReset();
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(content: Text('Demo reset')));
                  }
                }
              },
            ),
          ),
          const SizedBox(height: 4),
          Center(
            child: TextButton.icon(
              icon: const Icon(Icons.apps),
              label: const Text('Try another product'),
              onPressed: () => Navigator.of(context).popUntil((r) => r.isFirst),
            ),
          ),
        ]),
      ),
    );
  }
}

/// AppBar title with the ASAC mark — used by each product shell.
class AsacTitle extends StatelessWidget {
  final String text;
  const AsacTitle(this.text, {super.key});
  @override
  Widget build(BuildContext context) => Row(mainAxisSize: MainAxisSize.min, children: [
        ClipRRect(
          borderRadius: BorderRadius.circular(6),
          child: Image.asset('assets/images/logo-mark.png', height: 28, width: 28),
        ),
        const SizedBox(width: 8),
        Flexible(child: Text(text, overflow: TextOverflow.ellipsis)),
      ]);
}
