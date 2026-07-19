import 'dart:async';
import '../models/device_status.dart';
import '../models/history_entry.dart';
import '../models/cycle.dart';

/// Common shape for talking to the device, regardless of transport.
/// MqttService (cloud, via broker) and LocalService (SoftAP, direct HTTP/WS)
/// both implement this so screens/providers can depend on the interface
/// rather than a concrete transport.
abstract class DeviceService {
  Stream<DeviceStatus>       get statusStream;
  Stream<List<HistoryEntry>> get historyStream;
  Stream<List<Cycle>>        get cyclesStream;
  Stream<bool>                get connectedStream;

  bool get isConnected;

  Future<bool> connect();

  void manualOn();
  void manualOff();
  void manualLiters(double liters);
  void stopCycle();
  void pauseCycle();
  void resumeCycle();
  void getHistory();
  void getHistoryRange(DateTime from, DateTime to);
  void getCycles();
  void setCycles(List<Cycle> cycles);

  // Escape hatch for commands not part of the typed interface above (e.g.
  // force_local_mode) — works the same way regardless of which transport
  // is currently active.
  void sendRaw(Map<String, dynamic> payload);

  void dispose();
}
