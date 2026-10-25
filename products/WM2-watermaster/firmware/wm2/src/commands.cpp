#include "commands.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "app.h"
#include "history.h"
#include "hw.h"
#include "net.h"
#include "settings.h"
#include "timeutil.h"

using namespace wm2core;

static int valveArg(JsonObjectConst in) {
  int v = in["valve"] | 0;
  return (v == 1 || v == 2) ? v - 1 : -1;
}

String dispatchCommand(const String& json, const char* via) {
  JsonDocument req;
  JsonDocument resp;
  resp["v"] = 1;
  DeserializationError de = deserializeJson(req, json);
  if (de) {
    resp["ok"] = false;
    resp["err"] = String("bad json: ") + de.c_str();
    String s;
    serializeJson(resp, s);
    return s;
  }
  JsonObjectConst in = req.as<JsonObjectConst>();
  String cmd = in["cmd"] | "";
  const char* src = in["src"] | via;
  if (!in["id"].isNull()) resp["id"] = in["id"];
  resp["cmd"] = cmd;
  bool ok = true;
  String err;
  bool saveCfg = false, coreCfg = false;
  JsonObject data = resp["data"].to<JsonObject>();

  if (cmd == "wifi_scan") {  // slow: run without holding the lock
    net::scanStart();
    uint32_t t0 = millis();
    while (!net::scanResult(data["networks"].to<JsonArray>()) && millis() - t0 < 8000) delay(100);
    resp["ok"] = true;
    String s;
    serializeJson(resp, s);
    return s;
  }

  Lock lock;

  if (cmd == "get_state") {
    buildState(data);
  } else if (cmd == "device_info") {
    data["fw"] = FW_VERSION;
    data["chip"] = ESP.getChipModel();
    data["mac"] = WiFi.macAddress();
    data["flash_mb"] = ESP.getFlashChipSize() / (1024 * 1024);
    data["fs_used"] = history::usedBytes();
    data["fs_total"] = history::totalBytes();
    data["heap"] = ESP.getFreeHeap();
    data["uptime_s"] = millis() / 1000;
    data["host"] = cfg.hostname();
    data["base"] = cfg.base();
    data["broker"] = cfg.brokerName();
    data["broker_host"] = cfg.brokerHost();
    data["device_id"] = cfg.deviceId();
    data["mqtt"] = net::mqttUp();
    data["ip"] = net::ip();
    data["ap"] = net::apOn();
    data["ap_ssid"] = "WM2-" + cfg.zone + "-" + cfg.node;
  } else if (cmd == "get_config") {
    cfg.toJson(data, false);
  } else if (cmd == "set_config") {
    // Bulk settings change, same keys as get_config (app settings screen).
    cfg.fromJson(in["config"].as<JsonObjectConst>());
    saveCfg = coreCfg = true;
    if (!in["config"]["ssid"].isNull() || !in["config"]["mqtt_host"].isNull()) net::reconfigure();
  } else if (cmd == "valve_open") {
    int v = valveArg(in);
    uint32_t sec = (uint32_t)((in["minutes"] | 0.0f) * 60);
    float l = in["litres"] | 0.0f;
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else if (!core.manualOpen(v, sec, l)) { ok = false; err = core.lastError; }
    else {
      noteSource(v, src);
      data["state"] = core.water() || bypassOn() ? "opening" : "waiting_for_water";
    }
  } else if (cmd == "valve_close") {
    int v = valveArg(in);
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else if (!core.manualClose(v)) { ok = false; err = core.lastError; }
    else noteSource(v, src);
  } else if (cmd == "set_mode") {
    int v = valveArg(in);
    Mode m;
    if (v < 0 || !modeFromName(in["mode"], m)) { ok = false; err = "need valve 1/2 and mode program/manual/off"; }
    else { core.setMode(v, m); cfg.core.v[v].mode = m; saveCfg = true; noteSource(v, src); }
  } else if (cmd == "set_program") {
    int v = valveArg(in);
    Program p;
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else if (!programFromJson(in["program"], p, todayIndex(), err)) ok = false;
    else {
      cfg.core.v[v].prog = p;
      core.setProgram(v, p);
      if (in["set_mode"] | true) { cfg.core.v[v].mode = MODE_PROGRAM; core.setMode(v, MODE_PROGRAM); }
      saveCfg = true;
      noteSource(v, src);
      programToJson(p, data["program"].to<JsonObject>());
    }
  } else if (cmd == "get_program") {
    JsonArray a = data["programs"].to<JsonArray>();
    for (int v = 0; v < 2; v++) {
      JsonObject o = a.add<JsonObject>();
      o["valve"] = v + 1;
      o["mode"] = modeName(cfg.core.v[v].mode);
      programToJson(cfg.core.v[v].prog, o["program"].to<JsonObject>());
    }
  } else if (cmd == "clear_program") {
    int v = valveArg(in);
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else { Program p; cfg.core.v[v].prog = p; core.setProgram(v, p); saveCfg = true; }
  } else if (cmd == "bypass") {
    bool on = in["on"] | true;
    uint32_t minutes = in["minutes"] | (uint32_t)cfg.bypassDefaultMin;
    setBypass(on, minutes, src);
  } else if (cmd == "set_report") {
    if (!in["active_s"].isNull()) cfg.activeS = constrain((int)in["active_s"], 30, 1800);
    if (!in["heartbeat_s"].isNull()) cfg.heartbeatS = constrain((int)in["heartbeat_s"], 60, 14400);
    if (!in["idle_delay_s"].isNull()) cfg.idleDelayS = constrain((int)in["idle_delay_s"], 60, 3600);
    saveCfg = true;
  } else if (cmd == "set_power") {
    JsonDocument c;
    c["profile"] = in["profile"];
    c["check_min"] = in["check_min"];
    cfg.fromJson(c.as<JsonObjectConst>());
    saveCfg = true;
    data["note"] = "this build runs the responsive profile; saver comes in phase 3";
  } else if (cmd == "set_battery") {
    JsonDocument c;
    for (const char* k : {"chemistry", "capacity_ah", "v_warn", "v_save", "v_crit", "daily_ah"})
      if (!in[k].isNull()) c[k] = in[k];
    cfg.fromJson(c.as<JsonObjectConst>());
    saveCfg = true;
  } else if (cmd == "set_flow") {
    int i = (in["input"] | 0) - 1;
    if (i < 0 || i > 3) { ok = false; err = "input must be 1..4"; }
    else {
      if (!in["valve"].isNull()) cfg.core.flowValve[i] = (int8_t)((int)in["valve"] - 1);
      if (!in["pulses_per_litre"].isNull()) cfg.ppl[i] = in["pulses_per_litre"];
      saveCfg = coreCfg = true;
    }
  } else if (cmd == "set_valve_hw") {
    int v = valveArg(in);
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else {
      if (!in["pulse_ms"].isNull()) cfg.pulseMs[v] = constrain((int)in["pulse_ms"], 10, 500);
      if (!in["gap_s"].isNull()) cfg.gapS = constrain((float)in["gap_s"], 1.0f, 5.0f);
      if (!in["invert"].isNull()) cfg.invert[v] = in["invert"];
      if (!in["never_close"].isNull()) cfg.core.v[v].neverClose = in["never_close"];
      if (!in["on_water_loss"].isNull()) cfg.core.v[v].closeOnWaterLoss = !strcmp(in["on_water_loss"] | "keep", "close");
      if (!in["close_after_min"].isNull()) cfg.core.v[v].waterLossCloseMin = constrain((int)in["close_after_min"], 1, 240);
      if (!in["safe_state"].isNull()) cfg.safeClose = !strcmp(in["safe_state"] | "close", "close");
      saveCfg = coreCfg = true;
    }
  } else if (cmd == "set_options") {
    JsonDocument c;
    for (const char* k : {"sequential", "day_start_hour", "tz_min", "ps_settle_s", "low_pct", "high_pct",
                          "imbalance_pct", "alarm_delay_s", "bypass_default_min"})
      if (!in[k].isNull()) c[k] = in[k];
    cfg.fromJson(c.as<JsonObjectConst>());
    saveCfg = coreCfg = true;
  } else if (cmd == "get_history") {
    uint32_t to = in["to"] | nowUtc();
    uint32_t from = in["from"] | (to > 86400 ? to - 86400 : 0);
    history::query(from, to, in["res"] | "", cfg.tzMin, data);
  } else if (cmd == "get_days") {
    uint32_t today = todayIndex();
    uint32_t n = in["days"] | 7;
    history::days(today >= n ? today - n : 0, today, data["days"].to<JsonArray>());
  } else if (cmd == "get_events") {
    history::events(in["limit"] | 50, data["events"].to<JsonArray>());
  } else if (cmd == "rtc_sync") {
    uint32_t e = in["epoch"] | 0;
    if (e < 1735689600) { ok = false; err = "epoch (UTC seconds) required"; }
    else setTimeUtc(e, src);
  } else if (cmd == "wifi_config") {
    cfg.ssid = in["ssid"] | "";
    cfg.pass = in["pass"] | "";
    saveCfg = true;
    net::reconfigure();
  } else if (cmd == "mqtt_config") {
    // {"broker":"agrisense"|"girish", "host","port","user","pass", ...}. Without
    // "broker", the fields go to the active profile. farm/zone/node and ag_id
    // set the topic names.
    JsonDocument c;
    String which = in["broker"] | cfg.brokerName();
    bool ag = which != "girish";
    c["broker"] = ag ? "agrisense" : "girish";
    const char* pre = ag ? "ag_" : "mqtt_";
    for (const char* k : {"host", "port", "user", "pass"})
      if (!in[k].isNull()) c[String(pre) + k] = in[k];
    for (const char* k : {"mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass", "ag_host", "ag_port", "ag_user",
                          "ag_pass", "ag_id", "farm", "zone", "node"})
      if (!in[k].isNull()) c[k] = in[k];
    cfg.fromJson(c.as<JsonObjectConst>());
    saveCfg = true;
    data["broker"] = cfg.brokerName();
    data["base"] = cfg.base();
    net::reconfigure();
  } else if (cmd == "softap_on") {
    net::startAp(in["minutes"] | 15);
  } else if (cmd == "valve_test") {
    int v = valveArg(in);
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else if (hw::battVolts() < 9) { ok = false; err = "needs 12V on J6"; }
    else {
      hw::valvePulseRaw(v, true);
      delay((uint32_t)(cfg.gapS * 1000));
      hw::valvePulseRaw(v, false);
      hw::valveRequest(v, false);
    }
  } else if (cmd == "clear_history") {
    history::wipe();
  } else if (cmd == "reboot") {
    saveProgressNow();
    requestReboot(1000);
  } else if (cmd == "factory_reset") {
    LittleFS.remove("/config.json");
    history::wipe();
    requestReboot(1000);
  } else {
    ok = false;
    err = cmd.length() ? "unknown command" : "missing cmd";
  }

  if (coreCfg) applyCoreConfig();
  if (saveCfg && !cfg.save()) { ok = false; err = "could not save settings"; }
  resp["ok"] = ok;
  if (!ok) resp["err"] = err;
  if (data.size() == 0) resp.remove("data");
  if (ok && cmd != "get_state" && cmd != "get_history" && cmd != "get_events" && cmd != "get_config" &&
      cmd != "device_info" && cmd != "get_days" && cmd != "get_program" && cmd != "wifi_scan")
    stateChanged();
  String s;
  serializeJson(resp, s);
  return s;
}
