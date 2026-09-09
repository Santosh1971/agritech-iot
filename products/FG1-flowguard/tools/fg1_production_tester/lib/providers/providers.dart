import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'bench_config.dart';
import '../services/report_store.dart';
import '../runner/production_test_runner.dart';

/// Loaded once at app start (see main.dart) and overridden into the
/// widget tree -- see BenchConfig.load()/save().
final benchConfigProvider = StateNotifierProvider<BenchConfigNotifier, BenchConfig>(
  (ref) => BenchConfigNotifier(),
);

class BenchConfigNotifier extends StateNotifier<BenchConfig> {
  BenchConfigNotifier() : super(const BenchConfig()) {
    _load();
  }

  Future<void> _load() async {
    state = await BenchConfig.load();
  }

  Future<void> update(BenchConfig Function(BenchConfig) updater) async {
    state = updater(state);
    await state.save();
  }
}

final reportStoreProvider = Provider<ReportStore>((ref) => ReportStore());

/// One runner instance per app session -- recreated per bench config
/// change so it always points at the current settings.
final testRunnerProvider = ChangeNotifierProvider<ProductionTestRunner>((ref) {
  final config = ref.watch(benchConfigProvider);
  return ProductionTestRunner(config: config, reportStore: ref.watch(reportStoreProvider));
});
