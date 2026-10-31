import 'package:flutter/material.dart';

import '../services/house_store.dart';
import '../widgets/common.dart';
import 'add_house_screen.dart';
import 'house_screen.dart';

/// List of polyhouses (one controller each) with a live summary.
class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key, required this.store});
  final HouseStore store;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: store,
      builder: (context, _) => Scaffold(
        appBar: AppBar(
          title: Row(children: [
            Image.asset('assets/images/logo-mark.png', height: 32),
            const SizedBox(width: 10),
            const Text('ASAC FG1'),
          ]),
        ),
        body: store.houses.isEmpty
            ? _Empty(onAdd: () => _add(context))
            : ListView(
                padding: const EdgeInsets.all(12),
                children: [for (final h in store.houses) _HouseCard(store: store, house: h)],
              ),
        floatingActionButton: store.houses.isEmpty
            ? null
            : FloatingActionButton.extended(
                onPressed: () => _add(context),
                icon: const Icon(Icons.add),
                label: const Text('Add polyhouse'),
              ),
      ),
    );
  }

  void _add(BuildContext context) =>
      Navigator.push(context, MaterialPageRoute(builder: (_) => AddHouseScreen(store: store)));
}

class _Empty extends StatelessWidget {
  const _Empty({required this.onAdd});
  final VoidCallback onAdd;
  @override
  Widget build(BuildContext context) => Center(
        child: Padding(
          padding: const EdgeInsets.all(32),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            Image.asset('assets/images/logo-full.png', height: 90),
            const SizedBox(height: 24),
            const Text('Polyhouse misting control', style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
            const SizedBox(height: 8),
            const Text('Add the FG1 Controller of your polyhouse to start.', textAlign: TextAlign.center),
            const SizedBox(height: 24),
            FilledButton.icon(onPressed: onAdd, icon: const Icon(Icons.add), label: const Text('Add polyhouse')),
          ]),
        ),
      );
}

class _HouseCard extends StatelessWidget {
  const _HouseCard({required this.store, required this.house});
  final HouseStore store;
  final House house;

  @override
  Widget build(BuildContext context) {
    final c = store.conn(house);
    return ListenableBuilder(
      listenable: c,
      builder: (context, _) {
        final s = c.status == null ? null : St(c.status!);
        final cs = Theme.of(context).colorScheme;
        return Card(
          clipBehavior: Clip.antiAlias,
          child: InkWell(
            onTap: () => Navigator.push(context,
                MaterialPageRoute(builder: (_) => HouseScreen(store: store, house: house))),
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Row(children: [
                PumpBadge(on: s?.pumpOn ?? false, size: 52),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Text(house.name, style: Theme.of(context).textTheme.titleMedium),
                    const SizedBox(height: 4),
                    if (s != null) ...[
                      Text(
                          '${fmtT(s.controlT)}   ${fmtRh(s.controlRh)}   ·   ${s.mode.toUpperCase()}',
                          style: Theme.of(context).textTheme.bodyLarge),
                      Text(reasonText(s.reason),
                          style: TextStyle(color: cs.onSurfaceVariant), maxLines: 1, overflow: TextOverflow.ellipsis),
                    ] else
                      Text(house.deviceId.isEmpty ? 'Connecting…' : house.deviceId,
                          style: TextStyle(color: cs.onSurfaceVariant)),
                  ]),
                ),
                LinkChip(via: c.via, fresh: c.fresh),
              ]),
            ),
          ),
        );
      },
    );
  }
}
