#pragma once
#include <ArduinoJson.h>
#include <functional>
#include "Scheduler.h"
#include "IrrigationController.h"
#include "WiFiManager.h"
#include "ProgramStore.h"
#include "DeviceIdentity.h"
#include "Ds1307Clock.h"
#include "RelayNames.h"
#include "SequenceLibrary.h"
#include "Sensors.h"

// Shared command dispatch — called identically from the MQTT message
// callback and the local WebSocket handler, same pattern FG1 uses.
//
// Wire protocol mirrors FG1's app exactly (see products/FG1-flowguard's
// local_service.dart / mqtt_service.dart):
//   - Commands in:      {"cmd": "...", ...params}
//   - Command reply:    {"ok": bool, "cmd": "...", "data": ...}   (direct response to sender)
//   - Broadcast (local): {"type": "status"|"programs", "data": ...}
//   - Broadcast (MQTT):  raw status object / raw programs array, one per topic (no envelope)
//     — main.cpp builds the local envelope by wrapping the same JSON
//     buildStatusJson()/buildProgramsJson() produce, so there's exactly
//     one source of truth for the payload shape either way.
//
// Commands:
//   get_status
//   manual_set        {"channel":"dosing"|"valve1".."valve4", "state": bool}
//   trigger_program    {"id": N, "seq": 0}           — bypass schedule, run now
//   force_stop
//   list_programs
//   set_programs       {"programs": [...]}            — full replace, like FG1's set_cycles
//   simulate_power_loss / simulate_power_restore       — bench-test stand-in for real IN1
//                                                         wiring (§3.7) — no hardware sense
//                                                         line exists yet, see spec conversation.
//   wifi_config        {"ssid": "...", "password": "..."}
//   force_local_mode / resume_auto_mode
//   wifi_scan          — kicks off an async scan (WiFiScanner, ported from
//                         FG1); result arrives as a "wifi_scan_result" broadcast
//   rtc_sync           {"unix": N} — see Ds1307Clock.h for the unix-encoding convention
//   factory_reset      — clears saved programs/WiFi creds/checkpoint, reboots
//   set_relay_names    {"pump":"...","dosing":"...","valves":["...","...","...","..."]}
//                         — display labels only, roles stay fixed (§2.3)
//   list_sequence_library
//   set_sequence_library {"sequences":[...]}          — full replace, same pattern as set_programs.
//                         Library entries are COPIED into a program's own sequences when the app
//                         builds a program from them — see SequenceLibrary.h's doc comment.
//   device_info
//
// KNOWN GAPS (flagged, not silently guessed at):
//   - manual_set bypasses the Scheduler's arbitration with an active
//     auto sequence — direct IrrigationController calls, same caveat
//     the original scaffold flagged. Fine for bench/manual-only use;
//     needs real testing once both are exercised together.

using ReplyFn = std::function<void(const String&)>;

class CommandHandler {
public:
  CommandHandler(Scheduler& scheduler, IrrigationController& irrigation,
                 WiFiManager& wifi, ProgramStore& store,
                 Program* programSlots[], uint8_t& programCount,
                 Ds1307Clock& clock, RelayNames& names, SequenceLibrary& library, Sensors& sensors)
    : _scheduler(scheduler), _irrigation(irrigation), _wifi(wifi),
      _store(store), _programSlots(programSlots), _programCount(programCount),
      _clock(clock), _names(names), _library(library), _sensors(sensors) {}

  void handle(const String& jsonIn, ReplyFn reply) {
    // Every command that reaches the firmware, from ANY transport (local
    // WS, MQTT, or serial), logs here — the single place to watch on the
    // serial monitor to know whether something the app sent ever
    // actually arrived, independent of what the app itself claims.
    Serial.printf("[Cmd] <- %s\n", jsonIn.c_str());

    // Bug fix: relay/scheduler state used to only reach the app via the
    // 5s periodic broadcast — a manual_set clicked the relay instantly
    // but the app's UI didn't reflect it for up to 5 seconds. Any
    // command handled here now triggers an immediate status push (see
    // consumeStatusChanged(), polled every loop() in main.cpp) on top
    // of that periodic heartbeat, not instead of it.
    _statusChanged = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonIn);
    if (err) {
      reply(_errorReply("", "bad_json"));
      return;
    }
    String cmd = doc["cmd"] | "";

    if (cmd == "get_status") {
      reply(_wrapOk(cmd, _statusJson()));
    } else if (cmd == "manual_set") {
      reply(_handleManualSet(doc));
    } else if (cmd == "trigger_program") {
      reply(_handleTrigger(doc));
    } else if (cmd == "force_stop") {
      _scheduler.forceStop();
      reply(_okReply(cmd));
    } else if (cmd == "list_programs") {
      reply(_wrapOk(cmd, _programsJson()));
    } else if (cmd == "set_programs") {
      reply(_handleSetPrograms(doc));
    } else if (cmd == "simulate_power_loss") {
      _scheduler.onPowerStateChange(false);
      reply(_okReply(cmd));
    } else if (cmd == "simulate_power_restore") {
      _scheduler.onPowerStateChange(true);
      reply(_okReply(cmd));
    } else if (cmd == "wifi_config") {
      String ssid = doc["ssid"] | "";
      String pass = doc["password"] | "";
      if (ssid.length() == 0) { reply(_errorReply(cmd, "missing_ssid")); return; }
      _wifi.setCredentials(ssid, pass);
      reply(_okReply(cmd));
    } else if (cmd == "force_local_mode") {
      _wifi.setForcedLocal(true);
      reply(_okReply(cmd));
    } else if (cmd == "resume_auto_mode") {
      _wifi.setForcedLocal(false);
      reply(_okReply(cmd));
    } else if (cmd == "wifi_scan") {
      // Scanning itself is async and lives in main.cpp's loop() (it needs
      // to span many loop() iterations) — this just raises the request
      // flag and acks immediately; the actual result arrives later as a
      // "wifi_scan_result" broadcast, same as FG1.
      _wifiScanRequested = true;
      reply(_wrapOk(cmd, "{\"scanning\":true}"));
    } else if (cmd == "rtc_sync") {
      uint32_t unixTime = doc["unix"] | 0;
      _clock.syncFromUnix(unixTime);
      reply(_okReply(cmd));
    } else if (cmd == "factory_reset") {
      // Actual NVS clear + reboot happens in main.cpp, deferred briefly
      // so this ack has time to flush before the socket/connection dies.
      _factoryResetRequested = true;
      reply(_okReply(cmd));
    } else if (cmd == "set_relay_names") {
      reply(_handleSetRelayNames(doc));
    } else if (cmd == "list_sequence_library") {
      reply(_wrapOk(cmd, _libraryJson()));
    } else if (cmd == "set_sequence_library") {
      reply(_handleSetLibrary(doc));
    } else if (cmd == "device_info") {
      reply(_wrapOk(cmd, _deviceInfoJson()));
    } else {
      reply(_errorReply(cmd, "unknown_command"));
    }
  }

  // Raw (unwrapped) payloads — used directly for MQTT topics, and
  // wrapped in a {"type","data"} envelope by the caller for local WS
  // broadcasts. Single source of truth for the field shapes.
  String buildStatusJson() { return _statusJson(); }
  String buildProgramsJson() { return _programsJson(); }
  String buildLibraryJson() { return _libraryJson(); }

  // True exactly once after set_programs succeeds — main.cpp polls
  // this to know when to push a fresh "programs" broadcast to every
  // client/topic, then clears it.
  bool consumeProgramsChanged() {
    bool v = _programsChanged;
    _programsChanged = false;
    return v;
  }
  bool consumeLibraryChanged() {
    bool v = _libraryChanged;
    _libraryChanged = false;
    return v;
  }

  // Same one-shot dirty-flag pattern for the two commands that need
  // main.cpp's loop() to actually do something (kick off an async scan,
  // or clear NVS + reboot) rather than being fully handled inline here.
  bool consumeWifiScanRequested() {
    bool v = _wifiScanRequested;
    _wifiScanRequested = false;
    return v;
  }
  bool consumeFactoryResetRequested() {
    bool v = _factoryResetRequested;
    _factoryResetRequested = false;
    return v;
  }
  bool consumeStatusChanged() {
    bool v = _statusChanged;
    _statusChanged = false;
    return v;
  }

private:
  static int _channelFromName(const String& name) {
    if (name == "dosing") return IrrigationController::CH_DOSING;
    if (name == "valve1") return 0;
    if (name == "valve2") return 1;
    if (name == "valve3") return 2;
    if (name == "valve4") return 3;
    return -1;
  }

  static const char* _doseTimingStr(DoseTiming t) {
    switch (t) { case DoseTiming::START: return "start";
                 case DoseTiming::MID:   return "mid";
                 default:                return "end"; }
  }
  static DoseTiming _doseTimingFromStr(const String& s) {
    if (s == "start") return DoseTiming::START;
    if (s == "end")   return DoseTiming::END;
    return DoseTiming::MID;
  }

  // Shared by program sequences AND library entries — same Sequence
  // fields either way, kept in one place so the two never drift apart.
  static void _parseSequenceJson(JsonObject sj, Sequence& s) {
    strlcpy(s.name, (sj["name"] | "Seq"), sizeof(s.name));
    s.valveMask = sj["valveMask"] | 0;
    s.doseEnabled = sj["doseEnabled"] | false;
    s.doseTiming = _doseTimingFromStr(sj["doseTiming"] | "mid");
    s.doseDurationSec = sj["doseDurationSec"] | 600;
    String mode = sj["runMode"] | "time";
    s.runMode = (mode == "volume") ? RunMode::VOLUME_BASED : RunMode::TIME_BASED;
    s.runTargetSec = sj["runTargetSec"] | 1800;
    s.runTargetLiters = sj["runTargetLiters"] | 0;
  }
  static void _sequenceToJson(const Sequence& seq, JsonObject so) {
    so["name"] = seq.name;
    so["valveMask"] = seq.valveMask;
    so["doseEnabled"] = seq.doseEnabled;
    so["doseTiming"] = _doseTimingStr(seq.doseTiming);
    so["doseDurationSec"] = seq.doseDurationSec;
    so["runMode"] = (seq.runMode == RunMode::VOLUME_BASED) ? "volume" : "time";
    so["runTargetSec"] = seq.runTargetSec;
    so["runTargetLiters"] = seq.runTargetLiters;
  }

  String _handleSetLibrary(JsonDocument& doc) {
    JsonArray seqs = doc["sequences"].as<JsonArray>();
    if (seqs.isNull()) return _errorReply("set_sequence_library", "missing_sequences");

    SequenceLibrary::Entry entries[SequenceLibrary::MAX_ENTRIES];
    uint8_t n = 0;
    for (JsonObject sj : seqs) {
      if (n >= SequenceLibrary::MAX_ENTRIES) break;
      _parseSequenceJson(sj, entries[n].seq);
      n++;
    }
    _library.replaceAll(entries, n);
    _libraryChanged = true;
    return _okReply("set_sequence_library");
  }

  String _libraryJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < _library.count(); i++) {
      const SequenceLibrary::Entry* e = _library.entryAt(i);
      if (!e) continue;
      JsonObject o = arr.add<JsonObject>();
      o["id"] = e->id;
      _sequenceToJson(e->seq, o);
    }
    String out; serializeJson(doc, out);
    return out;
  }

  String _handleManualSet(JsonDocument& doc) {
    String channel = doc["channel"] | "";
    bool state = doc["state"] | false;
    if (channel == "dosing") {
      _irrigation.setDosing(state);
    } else if (channel.startsWith("valve")) {
      int idx = _channelFromName(channel);
      if (idx < 0) return _errorReply("manual_set", "unknown_channel");
      _irrigation.setValve((uint8_t)idx, state);
    } else {
      return _errorReply("manual_set", "unknown_channel");
    }
    return _okReply("manual_set");
  }

  String _handleSetRelayNames(JsonDocument& doc) {
    _names.pump = String((const char*)(doc["pump"] | "Pump"));
    _names.dosing = String((const char*)(doc["dosing"] | "Dosing"));
    JsonArray valves = doc["valves"].as<JsonArray>();
    uint8_t i = 0;
    for (JsonVariant v : valves) {
      if (i >= 4) break;
      _names.valve[i] = String((const char*)(v.as<const char*>()));
      i++;
    }
    _names.save();
    return _okReply("set_relay_names");
  }

  String _handleTrigger(JsonDocument& doc) {
    uint8_t id = doc["id"] | 0;
    uint8_t seq = doc["seq"] | 0;
    for (uint8_t i = 0; i < _programCount; i++) {
      if (_programSlots[i]->id == id) {
        _scheduler.triggerNow(_programSlots[i], seq);
        return _okReply("trigger_program");
      }
    }
    return _errorReply("trigger_program", "invalid_id");
  }

  // Full replace, mirroring FG1's set_cycles — the app always sends the
  // complete desired program list; simplest correct model given how
  // rarely this is called compared to status/manual traffic, and it
  // sidesteps partial add/edit/delete/reorder protocol complexity.
  String _handleSetPrograms(JsonDocument& doc) {
    JsonArray progs = doc["programs"].as<JsonArray>();
    if (progs.isNull()) return _errorReply("set_programs", "missing_programs");

    uint8_t newCount = 0;
    for (JsonObject pj : progs) {
      if (newCount >= ProgramStore::MAX_SLOTS) break;
      Program* p = _programSlots[newCount];
      memset(p, 0, sizeof(Program));
      p->id = newCount;  // positional id — stable as long as the list order/count doesn't change underneath a running sequence
      strlcpy(p->name, (pj["name"] | "Program"), sizeof(p->name));
      p->repeatMode = (String(pj["repeatMode"] | "interval") == "rotation")
                        ? RepeatMode::ROTATION : RepeatMode::INTERVAL_DAYS;
      p->intervalDays = pj["intervalDays"] | 1;
      p->startHour = pj["startHour"] | 6;
      p->startMinute = pj["startMinute"] | 0;
      p->enabled = pj["enabled"] | true;
      p->autoStart = pj["autoStart"] | true;

      JsonArray seqs = pj["sequences"].as<JsonArray>();
      uint8_t si = 0;
      for (JsonObject sj : seqs) {
        if (si >= 10) break;
        Sequence& s = p->sequences[si];
        s.id = si;
        _parseSequenceJson(sj, s);
        si++;
      }
      p->sequenceCount = si;
      newCount++;
    }

    _programCount = newCount;
    _store.saveAll(_programSlots, _programCount);
    // Bug fix: without this, the Scheduler never learns about programs
    // defined after boot — see resetPrograms()'s doc comment in
    // Scheduler.h. This is what actually makes autoStart/interval/
    // rotation triggering work for anything the app just saved.
    _scheduler.resetPrograms(_programSlots, _programCount);
    _programsChanged = true;
    return _okReply("set_programs");
  }

  String _programsJson() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < _programCount; i++) {
      Program* p = _programSlots[i];
      JsonObject o = arr.add<JsonObject>();
      o["id"] = p->id;
      o["name"] = p->name;
      o["enabled"] = p->enabled;
      o["autoStart"] = p->autoStart;
      o["repeatMode"] = (p->repeatMode == RepeatMode::ROTATION) ? "rotation" : "interval";
      o["intervalDays"] = p->intervalDays;
      o["startHour"] = p->startHour;
      o["startMinute"] = p->startMinute;
      JsonArray seqs = o["sequences"].to<JsonArray>();
      for (uint8_t s = 0; s < p->sequenceCount; s++) {
        JsonObject so = seqs.add<JsonObject>();
        so["id"] = p->sequences[s].id;
        _sequenceToJson(p->sequences[s], so);
      }
    }
    String out; serializeJson(doc, out);
    return out;
  }

  String _statusJson() {
    JsonDocument doc;
    doc["device_id"] = computeDeviceId();
    doc["state"] = (int)_scheduler.state();
    doc["pump"] = _irrigation.getPump();
    doc["dosing"] = _irrigation.getDosing();
    JsonArray valves = doc["valves"].to<JsonArray>();
    for (uint8_t i = 0; i < IrrigationController::VALVE_COUNT; i++) valves.add(_irrigation.getValve(i));
    doc["wifi_connected"] = _wifi.isStaConnected();
    doc["wifi_rssi"] = _wifi.isStaConnected() ? WiFi.RSSI() : 0;
    doc["forced_local"] = _wifi.isForcedLocal();
    doc["programs_count"] = _programCount;
    doc["rtc_date"] = _clock.dateString();
    doc["rtc_time"] = _clock.timeString();
    doc["rtc_ok"] = _clock.isRunning();

    // See Sensors.h: pressure/flow scale factors are documented
    // placeholders until real sensors are wired — values here are real
    // ADC/pulse reads, not yet calibrated engineering units.
    doc["pressure1_bar"] = _sensors.pressure1Bar();
    doc["pressure2_bar"] = _sensors.pressure2Bar();
    doc["flow_rate_lpm"] = _sensors.flowRateLpm();
    doc["flow_total_liters"] = _sensors.flowTotalLiters();
    doc["water_level_ok"] = _sensors.waterLevelOk();
    doc["battery_volts"] = _sensors.batteryVolts();

    JsonObject names = doc["relay_names"].to<JsonObject>();
    names["pump"] = _names.pump;
    names["dosing"] = _names.dosing;
    JsonArray valveNames = names["valves"].to<JsonArray>();
    for (uint8_t i = 0; i < 4; i++) valveNames.add(_names.valve[i]);

    const Program* active = _scheduler.activeProgram();
    if (active) {
      const Sequence& seq = active->sequences[_scheduler.activeSeqIndex()];
      doc["active_program_id"] = active->id;
      doc["active_program_name"] = active->name;
      doc["active_sequence_name"] = seq.name;
      doc["active_seq_index"] = _scheduler.activeSeqIndex();
      doc["elapsed_sec"] = _scheduler.elapsedRunSec();
      doc["run_target_sec"] = seq.runTargetSec;
    }

    String out; serializeJson(doc, out);
    return out;
  }

  String _deviceInfoJson() {
    JsonDocument doc;
    doc["device_id"] = computeDeviceId();
    doc["model"] = "WM1-Mini";
    doc["firmware"] = "0.2.0";
    String out; serializeJson(doc, out);
    return out;
  }

  String _wrapOk(const String& cmd, const String& innerJson) {
    // innerJson is already-serialized JSON (object or array) — splice it
    // in as raw text rather than round-tripping through another parse.
    String out = "{\"ok\":true,\"cmd\":\"" + cmd + "\",\"data\":" + innerJson + "}";
    return out;
  }
  String _okReply(const String& cmd) {
    JsonDocument doc; doc["cmd"] = cmd; doc["ok"] = true;
    String out; serializeJson(doc, out); return out;
  }
  String _errorReply(const String& cmd, const String& error) {
    JsonDocument doc; doc["cmd"] = cmd; doc["ok"] = false; doc["error"] = error;
    String out; serializeJson(doc, out); return out;
  }

  Scheduler& _scheduler;
  IrrigationController& _irrigation;
  WiFiManager& _wifi;
  ProgramStore& _store;
  Program** _programSlots;
  uint8_t& _programCount;
  Ds1307Clock& _clock;
  RelayNames& _names;
  SequenceLibrary& _library;
  Sensors& _sensors;
  bool _programsChanged = false;
  bool _libraryChanged = false;
  bool _statusChanged = false;
  bool _wifiScanRequested = false;
  bool _factoryResetRequested = false;
};
