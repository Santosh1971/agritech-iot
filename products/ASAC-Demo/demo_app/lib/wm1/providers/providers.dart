import 'dart:convert';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/device_service.dart';
import '../services/demo_service.dart';
import '../models/device_status.dart';
import '../models/program.dart';
import '../models/hardware_config.dart';

export '../../common/demo.dart' show themeModeProvider;

// Demo build: same provider names the real WM1-Mini app's screens use,
// all backed by the in-phone simulated controller.

enum TransportMode { local, cloud }

final wm1DemoServiceProvider = Provider<Wm1DemoService>((ref) {
  final svc = Wm1DemoService();
  ref.onDispose(svc.dispose);
  return svc;
});

final deviceServiceProvider = Provider<DeviceService>((ref) => ref.watch(wm1DemoServiceProvider));
final localServiceProvider = Provider<Wm1DemoService>((ref) => ref.watch(wm1DemoServiceProvider));
final transportModeProvider = Provider<TransportMode>((ref) => TransportMode.cloud);
final deviceSuffixProvider = Provider<String>((ref) => 'DEMO');
final deviceConnectedProvider = Provider<bool>((ref) => true);
final deviceOnlineProvider = Provider<bool>((ref) => true);
final localDebugLogProvider = Provider<List<String>>((ref) => const []);

const _hardwareConfigPrefsKey = 'wm1_demo_hardware_config';

/// Which optional sensors/dosing are shown — phone-side display choice.
/// The demo shows everything by default so customers see the full system.
class HardwareConfigNotifier extends StateNotifier<HardwareConfig> {
  HardwareConfigNotifier()
      : super(const HardwareConfig(hasPressure2: true)) {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_hardwareConfigPrefsKey);
    if (raw == null) return;
    try {
      state = HardwareConfig.fromJson(jsonDecode(raw) as Map<String, dynamic>);
    } catch (_) {}
  }

  Future<void> update(HardwareConfig config) async {
    state = config;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_hardwareConfigPrefsKey, jsonEncode(config.toJson()));
  }
}

final hardwareConfigProvider =
    StateNotifierProvider<HardwareConfigNotifier, HardwareConfig>((ref) => HardwareConfigNotifier());

final deviceStatusProvider = StreamProvider<DeviceStatus>((ref) {
  return ref.watch(deviceServiceProvider).statusStream;
});

final localDeviceStatusProvider = deviceStatusProvider;

final programsProvider = StreamProvider<List<Program>>((ref) {
  return ref.watch(deviceServiceProvider).programsStream;
});

final libraryProvider = StreamProvider<List<LibrarySequence>>((ref) {
  return ref.watch(deviceServiceProvider).libraryStream;
});
