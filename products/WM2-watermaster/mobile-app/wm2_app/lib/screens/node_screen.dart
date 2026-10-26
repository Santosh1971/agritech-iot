import 'package:flutter/material.dart';

import '../services/node_store.dart';
import 'history_tab.dart';
import 'programs_tab.dart';
import 'settings_tab.dart';
import 'status_tab.dart';

class NodeScreen extends StatefulWidget {
  const NodeScreen({super.key, required this.entry, required this.store, this.shared});
  final NodeEntry entry;
  final NodeStore store;
  /// Controller owned by the farm dashboard; reused instead of opening a second link.
  final NodeController? shared;

  @override
  State<NodeScreen> createState() => _NodeScreenState();
}

class _NodeScreenState extends State<NodeScreen> {
  late final NodeController ctl = widget.shared ?? NodeController(widget.entry, widget.store);
  int tab = 0;

  @override
  void initState() {
    super.initState();
    if (!ctl.connected) ctl.connect();
  }

  @override
  void dispose() {
    if (widget.shared == null) ctl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: ctl,
      builder: (context, _) {
        final s = ctl.state;
        return Scaffold(
          appBar: AppBar(
            title: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(widget.entry.name),
              Text(
                [ctl.status, if (s?['local'] != null) s!['local']].join('  |  '),
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ]),
            actions: [
              IconButton(icon: const Icon(Icons.refresh), tooltip: 'Reconnect', onPressed: ctl.connect),
            ],
          ),
          body: !ctl.connected && s == null
              ? Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Column(mainAxisSize: MainAxisSize.min, children: [
                      if (ctl.connecting) const CircularProgressIndicator(),
                      const SizedBox(height: 16),
                      Text(ctl.status, textAlign: TextAlign.center),
                      const SizedBox(height: 16),
                      if (!ctl.connecting) FilledButton(onPressed: ctl.connect, child: const Text('Try again')),
                    ]),
                  ),
                )
              : IndexedStack(index: tab, children: [
                  StatusTab(ctl: ctl),
                  ProgramsTab(ctl: ctl),
                  HistoryTab(ctl: ctl),
                  SettingsTab(ctl: ctl),
                ]),
          bottomNavigationBar: NavigationBar(
            selectedIndex: tab,
            onDestinationSelected: (i) => setState(() => tab = i),
            destinations: const [
              NavigationDestination(icon: Icon(Icons.dashboard_outlined), label: 'Status'),
              NavigationDestination(icon: Icon(Icons.calendar_month_outlined), label: 'Programs'),
              NavigationDestination(icon: Icon(Icons.history), label: 'History'),
              NavigationDestination(icon: Icon(Icons.settings_outlined), label: 'Settings'),
            ],
          ),
        );
      },
    );
  }
}

/// Small helpers shared by the tabs.
void showReply(BuildContext context, Map<String, dynamic> r, String okText) {
  if (!context.mounted) return;
  ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(r['ok'] == true ? okText : 'Failed: ${r['err'] ?? 'unknown error'}')));
}

String fmtTs(dynamic ts) {
  if (ts is! num || ts == 0) return '-';
  final d = DateTime.fromMillisecondsSinceEpoch(ts.toInt() * 1000);
  String two(int v) => v.toString().padLeft(2, '0');
  return '${d.day}/${d.month} ${two(d.hour)}:${two(d.minute)}:${two(d.second)}';
}

String eventText(Map<String, dynamic> e) {
  switch (e['event']) {
    case 'valve':
      return 'V${e['valve']} ${e['open'] == true ? 'opened' : 'closed'} (${e['reason']}${e['src'] != null ? ', ${e['src']}' : ''})';
    case 'water':
      return e['present'] == true ? 'Water arrived' : 'Water gone';
    case 'alarm':
      return 'Alarm ${e['code']} ${e['active'] == true ? 'ON' : 'cleared'}${e['valve'] != null ? ' V${e['valve']}' : ''}';
    case 'day_result':
      return 'Day result V${e['valve']}: ${e['status']}, ${e['run_min']}/${e['planned_min'] ?? e['planned_l']}';
    case 'bypass':
      return 'Bypass ${e['on'] == true ? 'on' : 'off'}';
    case 'manual_done':
      return 'Manual run done V${e['valve']}';
    case 'battery':
      return 'Battery ${e['level']} (${e['v']} V)';
    default:
      return '${e['event']}';
  }
}
