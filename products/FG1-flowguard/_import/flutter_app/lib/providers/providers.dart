import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/device_service.dart';
import '../services/mqtt_service.dart';
import '../services/local_service.dart';
import '../models/device_status.dart';
import '../models/cycle.dart';
import '../models/history_entry.dart';

enum TransportMode { local, cloud }

const _transportPrefsKey = 'transport_mode';
const _themePrefsKey = 'theme_mode';
const _deviceSuffixPrefsKey = 'device_suffix';

// The 4-char MAC suffix identifying which physical device this app talks
// to over Cloud/MQTT (see device_id shown in Settings, or the device's own
// SoftAP name — both show the same suffix). Persisted so it survives app
// restarts; defaults empty until the user sets it, since guessing wrong
// would silently talk to a different device.
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
    StateNotifierProvider<DeviceSuffixNotifier, String>(
        (ref) => DeviceSuffixNotifier());

// Individual transports -- both always exist; only one is "active" at a
// time per transportModeProvider, but keeping both alive means switching
// doesn't lose any in-flight connection state.
// Rebuilds (disposing the old instance, per ref.onDispose below) whenever
// the target device suffix changes, so topics always match the currently
// selected device rather than needing an app restart.
final mqttServiceProvider = Provider<MqttService>((ref) {
  final suffix = ref.watch(deviceSuffixProvider);
  final svc = MqttService(deviceSuffix: suffix);
  ref.onDispose(() => svc.dispose());
  return svc;
});

final localServiceProvider = Provider<LocalService>((ref) {
  final svc = LocalService();
  ref.onDispose(() => svc.dispose());
  return svc;
});

// Manual transport switch -- SoftAP (local) is the default, per design
// decision: most setup/testing happens on the device's own network, and
// this avoids surprising a no-internet customer with a cloud attempt.
// Persisted so the choice survives app restarts.
class TransportModeNotifier extends StateNotifier<TransportMode> {
  TransportModeNotifier() : super(TransportMode.local) {
    _load();
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    final saved = prefs.getString(_transportPrefsKey);
    if (saved == 'cloud') state = TransportMode.cloud;
  }

  Future<void> setMode(TransportMode mode) async {
    state = mode;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
        _transportPrefsKey, mode == TransportMode.cloud ? 'cloud' : 'local');
  }
}

final transportModeProvider =
    StateNotifierProvider<TransportModeNotifier, TransportMode>(
        (ref) => TransportModeNotifier());

// The "active" transport -- this is what screens should actually use.
// Resolves to whichever concrete service matches the current mode.
final deviceServiceProvider = Provider<DeviceService>((ref) {
  final mode = ref.watch(transportModeProvider);
  return mode == TransportMode.local
      ? ref.watch(localServiceProvider)
      : ref.watch(mqttServiceProvider);
});

// Connection state -- debounced. LocalService's WebSocket can drop and
// auto-reconnect within ~3s (see local_service.dart's _scheduleReconnect),
// and commands often keep working through those brief blips — but a raw
// connectedStream flips to false the instant the socket drops, flashing
// "Device Offline" even though the device is actually still responding a
// moment later. This only surfaces "offline" after being disconnected
// continuously for longer than the auto-reconnect would need, mirroring
// the same hysteresis principle the firmware already uses for its own
// cloud/local transitions.
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
      _offlineTimer = Timer(const Duration(seconds: 6), () {
        state = false;
      });
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

final cyclesProvider = StreamProvider<List<Cycle>>((ref) {
  return ref.watch(deviceServiceProvider).cyclesStream;
});

final historyProvider = StreamProvider<List<HistoryEntry>>((ref) {
  return ref.watch(deviceServiceProvider).historyStream;
});

// Dark/light mode -- defaults to following the system setting, persisted
// once the user picks something explicit. Same StateNotifier + prefs
// pattern as transportModeProvider above.
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
      ThemeMode.light  => 'light',
      ThemeMode.dark   => 'dark',
      ThemeMode.system => 'system',
    };
    await prefs.setString(_themePrefsKey, str);
  }
}

final themeModeProvider =
    StateNotifierProvider<ThemeModeNotifier, ThemeMode>(
        (ref) => ThemeModeNotifier());
