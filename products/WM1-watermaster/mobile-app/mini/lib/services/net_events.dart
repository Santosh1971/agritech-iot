import 'package:flutter/services.dart';

/// What the phone's WiFi looks like right now (from MainActivity.kt's
/// passive network callback). Android only; on other platforms the
/// stream simply never emits.
class NetState {
  final bool wifi;      // on some WiFi network
  final bool internet;  // that WiFi has validated internet access
  const NetState(this.wifi, this.internet);
}

class NetEvents {
  static const _channel = EventChannel('wm1/netevents');

  static Stream<NetState> stream() => _channel.receiveBroadcastStream().map((e) {
        final m = Map<String, dynamic>.from(e as Map);
        return NetState(m['wifi'] == true, m['internet'] == true);
      });
}
