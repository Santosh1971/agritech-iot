import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Demo stand-in for the WPC app's Backend: no cloud / local link to pick,
/// just the two dashboard display toggles the screens read.
class Backend extends ChangeNotifier {
  static final Backend instance = Backend._();
  Backend._() {
    _load();
  }

  static const _kShowPower = 'wpc_demo_show_power';
  static const _kShowFlow = 'wpc_demo_show_flow';

  bool showPowerStatus = true;
  bool showWaterFlow = true;

  Future<void> _load() async {
    final p = await SharedPreferences.getInstance();
    showPowerStatus = p.getBool(_kShowPower) ?? true;
    showWaterFlow = p.getBool(_kShowFlow) ?? true;
    notifyListeners();
  }

  Future<void> setShowPowerStatus(bool v) async {
    showPowerStatus = v;
    notifyListeners();
    (await SharedPreferences.getInstance()).setBool(_kShowPower, v);
  }

  Future<void> setShowWaterFlow(bool v) async {
    showWaterFlow = v;
    notifyListeners();
    (await SharedPreferences.getInstance()).setBool(_kShowFlow, v);
  }
}
