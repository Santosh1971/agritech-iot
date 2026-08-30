import 'dart:async';
import 'package:flutter/services.dart';

/// Talks to MainActivity.kt's ConnectivityManager.bindProcessToNetwork
/// logic — see that file for the full explanation of why this exists.
/// Android-only; no-op (returns false / does nothing) on other platforms.
class NetworkBinding {
  static const _channel = MethodChannel('wm1/network');

  /// Binds this app's network traffic to whatever WiFi network is
  /// currently connected, regardless of whether it has internet.
  /// Returns true once genuinely bound, false on timeout/failure/error.
  /// Call before opening the local WebSocket, every time — cheap, and
  /// covers the case where the phone switched WiFi networks since the
  /// last bind.
  static Future<bool> bindWifi() async {
    try {
      // Belt-and-suspenders on top of the native side's own 5s internal
      // timeout: if the platform channel call itself never returns for
      // any reason (message queue stall, an Android version where the
      // native timeout logic doesn't fire as expected), this guarantees
      // the caller isn't stuck awaiting forever — a hang here would
      // otherwise mean _connectInternal's finally block never runs,
      // silently killing the entire auto-retry loop for good.
      final result = await _channel.invokeMethod<bool>('bindWifi').timeout(const Duration(seconds: 7));
      return result ?? false;
    } catch (e) {
      return false;
    }
  }

  /// Releases the binding so the rest of the app (Cloud/MQTT mode,
  /// which needs a network that actually has internet) goes back to
  /// Android's normal network selection. Call when switching to Cloud
  /// mode — otherwise the app stays stuck on the no-internet WiFi.
  static Future<void> unbind() async {
    try {
      await _channel.invokeMethod('unbind');
    } catch (_) {}
  }
}
