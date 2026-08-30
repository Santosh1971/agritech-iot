import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/device_service.dart';
import '../services/mqtt_service.dart';
import '../services/local_service.dart';
import '../models/device_status.dart';
import '../models/program.dart';
import '../models/hardware_config.dart';

enum TransportMode { local, cloud }

const _transportPrefsKey = 'transport_mode';
const _themePrefsKey = 'theme_mode';
const _deviceSuffixPrefsKey = 'device_suffix';
const _hardwareConfigPrefsKey = 'hardware_config';

/// The 4-char MAC suffix identifying which physical device this app
/// talks to over Cloud/MQTT (shown on the device's own SoftAP name,
/// WM1_XXXX, and in Settings > Device Info once connected locally).
class DeviceSuffixNotifier extends StateNotifier<String> {
  DeviceSuffixNotifier() : super('') {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    state = prefs.getString(_deviceSuffixPrefsKey) ?? '';
  }
  Future<void> setSuffix(String suffix) async {
    state = suffix;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_deviceSuffixPrefsKey, suffix);
  }
}

final deviceSuffixProvider =
    StateNotifierProvider<DeviceSuffixNotifier, String>((ref) => DeviceSuffixNotifier());

/// Which optional sensors/dosing are actually wired up — see
/// HardwareConfig's doc comment. Purely a local display preference,
/// never sent to the device.
class HardwareConfigNotifier extends StateNotifier<HardwareConfig> {
  HardwareConfigNotifier() : super(const HardwareConfig()) {
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

final mqttServiceProvider = Provider<MqttService>((ref) {
  final suffix = ref.watch(deviceSuffixProvider);
  final svc = MqttService(deviceSuffix: suffix);
  ref.onDispose(() => svc.dispose());
  return svc;
});

// Local mode always targets the device's own SoftAP gateway — there is
// nothing here for the user to configure, and no "device address" field
// is shown anywhere in the app anymore. LocalService reconnects on its
// own (see local_service.dart), so simply being on the device's WiFi is
// enough for Local mode to work with zero taps.
final localServiceProvider = Provider<LocalService>((ref) {
  final svc = LocalService();
  ref.onDispose(() => svc.dispose());
  return svc;
});

// Always reflects whatever's on the LOCAL transport, regardless of the
// app's current mode switch — used by the Target Device picker (so it
// can offer "use the device you're locally connected to" even while
// the mode switch itself is set to Cloud) and by Local Setup screens.
final localDeviceStatusProvider = StreamProvider<DeviceStatus>((ref) {
  return ref.watch(localServiceProvider).statusStream;
});

// Rolling connection-lifecycle log (connecting/connected/handshake
// failed/reconnecting) shown directly in the app — so "is it actually
// working" doesn't require a laptop and a serial cable to answer.
class DebugLogNotifier extends StateNotifier<List<String>> {
  final Ref ref;
  StreamSubscription<String>? _sub;

  // Seeded from LocalService's own buffered history (see logHistory's
  // doc comment) — this is what makes the log show everything that
  // happened since app launch, not just events after this screen opened.
  DebugLogNotifier(this.ref) : super(ref.read(localServiceProvider).logHistory) {
    _sub = ref.read(localServiceProvider).debugStream.listen(_onEvent);
  }

  void _onEvent(String line) {
    state = [...state, line];
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}

final localDebugLogProvider =
    StateNotifierProvider<DebugLogNotifier, List<String>>((ref) => DebugLogNotifier(ref));

/// Manual transport switch — Local (SoftAP/LAN) is the default, since
/// most bench/field setup happens directly against the device before
/// (or without) a cloud broker being reachable. Persisted across restarts.
class TransportModeNotifier extends StateNotifier<TransportMode> {
  TransportModeNotifier() : super(TransportMode.local) {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    if (prefs.getString(_transportPrefsKey) == 'cloud') state = TransportMode.cloud;
  }
  Future<void> setMode(TransportMode mode) async {
    state = mode;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_transportPrefsKey, mode == TransportMode.cloud ? 'cloud' : 'local');
  }
}

final transportModeProvider =
    StateNotifierProvider<TransportModeNotifier, TransportMode>((ref) => TransportModeNotifier());

final deviceServiceProvider = Provider<DeviceService>((ref) {
  final mode = ref.watch(transportModeProvider);
  return mode == TransportMode.local
      ? ref.watch(localServiceProvider)
      : ref.watch(mqttServiceProvider);
});

/// Debounced connection state — a brief WS drop/reconnect (or MQTT
/// blip) shouldn't flash "Device Offline" for something that resolves
/// within a few seconds.
class ConnectedNotifier extends StateNotifier<bool> {
  final Ref ref;
  Timer? _offlineTimer;
  StreamSubscription<bool>? _sub;

  ConnectedNotifier(this.ref) : super(false) {
    _sub = ref.read(deviceServiceProvider).connectedStream.listen(_onEvent);
    ref.listen(deviceServiceProvider, (prev, next) {
      _sub?.cancel();
      _sub = next.connectedStream.listen(_onEvent);
    });
  }

  void _onEvent(bool connected) {
    if (connected) {
      _offlineTimer?.cancel();
      state = true;
    } else {
      _offlineTimer?.cancel();
      _offlineTimer = Timer(const Duration(seconds: 6), () => state = false);
    }
  }

  @override
  void dispose() {
    _offlineTimer?.cancel();
    _sub?.cancel();
    super.dispose();
  }
}

final deviceConnectedProvider =
    StateNotifierProvider<ConnectedNotifier, bool>((ref) => ConnectedNotifier(ref));

final deviceStatusProvider = StreamProvider<DeviceStatus>((ref) {
  return ref.watch(deviceServiceProvider).statusStream;
});

final programsProvider = StreamProvider<List<Program>>((ref) {
  return ref.watch(deviceServiceProvider).programsStream;
});

final libraryProvider = StreamProvider<List<LibrarySequence>>((ref) {
  return ref.watch(deviceServiceProvider).libraryStream;
});

class ThemeModeNotifier extends StateNotifier<ThemeMode> {
  ThemeModeNotifier() : super(ThemeMode.system) {
    _load();
  }
  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final saved = prefs.getString(_themePrefsKey);
    if (saved == 'light') state = ThemeMode.light;
    if (saved == 'dark') state = ThemeMode.dark;
  }
  Future<void> setMode(ThemeMode mode) async {
    state = mode;
    final prefs = await SharedPreferences.getInstance();
    final str = switch (mode) {
      ThemeMode.light => 'light',
      ThemeMode.dark => 'dark',
      ThemeMode.system => 'system',
    };
    await prefs.setString(_themePrefsKey, str);
  }
}

final themeModeProvider =
    StateNotifierProvider<ThemeModeNotifier, ThemeMode>((ref) => ThemeModeNotifier());
