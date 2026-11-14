/// Browsers can't sweep the local network: nothing to find.
Future<String?> findNodeOnLan(String deviceId) async => null;

Future<bool> phoneOnWifi() async => true;
