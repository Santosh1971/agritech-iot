import 'dart:async';
import '../models/device_status.dart';
import '../models/program.dart';

/// Common shape for talking to the WM1 device, regardless of transport.
/// MqttService (cloud, via broker) and LocalService (SoftAP, direct
/// WebSocket) both implement this so screens/providers depend on the
/// interface, not a concrete transport — same split FG1 uses.
abstract class DeviceService {
  Stream<DeviceStatus> get statusStream;
  Stream<List<Program>> get programsStream;
  Stream<List<LibrarySequence>> get libraryStream;
  Stream<bool> get connectedStream;
  Stream<bool> get deviceOnlineStream;

  bool get isConnected;

  Future<bool> connect();

  void manualSet(String channel, bool state); // channel: "dosing"|"valve1".."valve4"
  void triggerProgram(int programId, int seqIndex);
  void forceStop();
  void pause();
  void resume();
  void getPrograms();
  void setPrograms(List<Program> programs);
  void getLibrary();
  void setLibrary(List<LibrarySequence> library);
  void simulatePowerLoss();
  void simulatePowerRestore();

  // Escape hatch for commands not part of the typed interface above
  // (wifi_config, force_local_mode, resume_auto_mode, device_info).
  void sendRaw(Map<String, dynamic> payload);

  void dispose();
}
