import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../services/device_service.dart';
import '../services/demo_service.dart';
import '../models/device_status.dart';
import '../models/cycle.dart';
import '../models/history_entry.dart';

export '../../common/demo.dart' show themeModeProvider;

// Demo build: same provider names the real FG1 app's screens use, but
// every one of them is backed by the in-phone simulated controller.

final fg1DemoServiceProvider = Provider<Fg1DemoService>((ref) {
  final svc = Fg1DemoService();
  ref.onDispose(svc.dispose);
  return svc;
});

final deviceServiceProvider =
    Provider<DeviceService>((ref) => ref.watch(fg1DemoServiceProvider));

// Only used as a cache-key prefix by a couple of screens.
final deviceSuffixProvider = Provider<String>((ref) => 'DEMO');

final deviceConnectedProvider = Provider<bool>((ref) => true);
final deviceOnlineProvider = Provider<bool>((ref) => true);

final deviceStatusProvider = StreamProvider<DeviceStatus>((ref) {
  return ref.watch(deviceServiceProvider).statusStream;
});

final cyclesProvider = StreamProvider<List<Cycle>>((ref) {
  return ref.watch(deviceServiceProvider).cyclesStream;
});

final historyProvider = StreamProvider<List<HistoryEntry>>((ref) {
  return ref.watch(deviceServiceProvider).historyStream;
});
