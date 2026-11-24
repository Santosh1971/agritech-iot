import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/device_service.dart';
import '../services/mqtt_service.dart';
import '../services/local_service.dart';
import '../services/net_events.dart';
import '../models/device_status.dart';
import '../models/program.dart';
import '../models/hardware_config.dart';

enum TransportMode { local, cloud }

const _transportPrefsKey = 'transport_mode';
const _autoTransportPrefsKey = 'transport_auto';
const _themePrefsKey = 'theme_mode';
const _deviceSuffixPrefsKey = 'device_suffix';
const _hardwareConfigPrefsKey = 'hardware_config';

/// The 4-char MAC suffix identifying which physical device this app
/// talks to over Cloud/MQTT (shown on the device's own SoftAP name,
/// WM1_XXXXXXXX, and in Settings > Device Info once connected locally).
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
///
/// Bug fix: switching modes here used to only swap WHICH service
/// deviceServiceProvider points to — nothing ever called connect() on
/// the newly-selected one. DashboardScreen's connect() only fires ONCE
/// per app launch (guarded by _didInitialConnect), so a mode switch
/// mid-session left the new transport sitting unconnected forever:
/// confirmed live as "switching to Cloud doesn't update until you
/// force-close and reopen the app" (the fresh launch is what re-fires
/// the one-time connect, this time against the now-persisted mode) —
/// and separately explained an apparent "schedule got deleted" report,
/// which was actually getPrograms() silently no-op'ing against an
/// unconnected MqttService, not an actual deletion. Now setMode()
/// itself triggers connect() on whichever service is newly active.
class TransportModeNotifier extends StateNotifier<TransportMode> {
  final Ref ref;
  TransportModeNotifier(this.ref) : super(TransportMode.local) {
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
    // Stop the transport we're leaving BEFORE starting the new one —
    // left running, its retry loop fights the new one over the
    // process-wide WiFi binding (MQTT unbinds, Local re-binds), which
    // is why a mid-session mode/network switch only ever worked after
    // restarting the app.
    if (mode == TransportMode.cloud) {
      ref.read(localServiceProvider).disconnect();
    } else {
      ref.read(mqttServiceProvider).disconnect();
    }
    ref.read(deviceServiceProvider).connect();
  }
}

/// Automatic connection mode (app 1.0.5, tester request: moving the phone
/// between the office WiFi and the board's SoftAP needed a mode change and
/// often an app restart). On by default; picking Local or Cloud by hand in
/// Settings turns it off.
///   WiFi without internet (the board's WM1_xxxx SoftAP) -> Local
///   WiFi with internet, or no WiFi (mobile data)        -> Cloud
/// A newly joined office WiFi is "without internet" for a few seconds until
/// Android validates it, so Local is only chosen after the state has held
/// for 6 s; Cloud is chosen after 1.5 s.
class AutoModeNotifier extends StateNotifier<bool> {
  final Ref ref;
  StreamSubscription<NetState>? _sub;
  Timer? _decide;
  NetState? _last;

  AutoModeNotifier(this.ref) : super(true) {
    _load();
    try {
      _sub = NetEvents.stream().listen(_onNet, onError: (_) {});
    } catch (_) {}
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    state = prefs.getBool(_autoTransportPrefsKey) ?? true;
  }

  Future<void> setAuto(bool on) async {
    state = on;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool(_autoTransportPrefsKey, on);
    if (on && _last != null) _onNet(_last!);
  }

  void _onNet(NetState n) {
    _last = n;
    if (!state) return;
    final want = (n.wifi && !n.internet) ? TransportMode.local : TransportMode.cloud;
    _decide?.cancel();
    _decide = Timer(Duration(milliseconds: want == TransportMode.local ? 6000 : 1500), () {
      if (!state || !mounted) return;
      if (ref.read(transportModeProvider) != want) {
        ref.read(transportModeProvider.notifier).setMode(want);
      } else {
        // Same mode but a different network (e.g. a new WiFi with
        // internet): kick the active transport so it reconnects now
        // instead of waiting for its retry timer.
        ref.read(deviceServiceProvider).connect();
      }
    });
  }

  @override
  void dispose() {
    _decide?.cancel();
    _sub?.cancel();
    super.dispose();
  }
}

final autoModeProvider =
    StateNotifierProvider<AutoModeNotifier, bool>((ref) => AutoModeNotifier(ref));

final transportModeProvider =
    StateNotifierProvider<TransportModeNotifier, TransportMode>((ref) => TransportModeNotifier(ref));

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
