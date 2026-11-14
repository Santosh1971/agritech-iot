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
#include "web.h"
#include "zone.h"
#include "wm2_pins.h"

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

  // A command the broker held while the node was offline can arrive hours
  // late (the session is kept so commands are not lost). Actions sent more
  // than 5 min ago are refused; reads (get_*, device_info, wifi_scan) still run.
  // The app stamps "ts" (UTC seconds); commands without it are not checked.
  uint32_t sentAt = in["ts"] | 0;
  if (sentAt && !strcmp(via, "mqtt") && timeValid() && nowUtc() > sentAt + 300 && !cmd.startsWith("get_") &&
      cmd != "device_info" && cmd != "wifi_scan") {
    resp["ok"] = false;
    resp["err"] = "stale command (sent " + String((nowUtc() - sentAt) / 60) + " min ago), not done";
    String s;
    serializeJson(resp, s);
    Serial.printf("[cmd] ignored stale %s from %s, %lu s old\n", cmd.c_str(), src, (unsigned long)(nowUtc() - sentAt));
    return s;
  }

  if (cmd == "wifi_scan") {
    // Replies at once with the cached list; "refresh" (default) starts a new
    // scan whose result is pushed as {"type":"wifi_scan"} and cached (WM2-004).
    if (in["refresh"] | true) net::scanRequest();
    net::scanCached(data["networks"].to<JsonArray>());
    data["age_s"] = net::scanAgeS();
    data["scanning"] = net::scanBusy();
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
    data["ota"] = web::otaEnabled();  // firmware update over WiFi available
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
    data["ap_ssid"] = cfg.apSsid();
  } else if (cmd == "get_config") {
    cfg.toJson(data, false);
  } else if (cmd == "set_config") {
    // Bulk settings change, same keys as get_config (app settings screen).
    cfg.fromJson(in["config"].as<JsonObjectConst>());
    saveCfg = coreCfg = true;
    if (!in["config"]["ssid"].isNull() || !in["config"]["broker"].isNull() || !in["config"]["zone"].isNull() ||
        !in["config"]["node"].isNull())
      net::reconfigure();
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
    for (const char* k : {"sequential", "ps_fitted", "day_start_hour", "tz_min", "ps_settle_s", "low_pct", "high_pct",
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
    String ssid = in["ssid"] | "";
    ssid.trim();
    if (!ssid.length()) {
      // A blank name would leave the node on its SoftAP for good; forgetting
      // the farm WiFi is wifi_forget ("Disconnect WiFi") only.
      ok = false;
      err = "WiFi name is empty: pick a network with Scan";
    } else {
      cfg.ssid = ssid;
      cfg.pass = in["pass"] | "";
      saveCfg = true;
      net::reconfigure();
      JsonDocument e;
      e["event"] = "wifi";
      e["what"] = "saved";
      e["ssid"] = ssid;
      e["src"] = src;
      emitEvent(e);
    }
  } else if (cmd == "mqtt_config") {
    // {"broker":"agrisense"|"girish", "zone","node"}. Host, port and login are
    // fixed per profile; any other keys are ignored.
    JsonDocument c;
    if (!in["broker"].isNull()) c["broker"] = strcmp(in["broker"] | "", "girish") ? "agrisense" : "girish";
    for (const char* k : {"zone", "node"})
      if (strlen(in[k] | "") > 0) c[k] = in[k];
    cfg.fromJson(c.as<JsonObjectConst>());
    saveCfg = true;
    data["broker"] = cfg.brokerName();
    data["base"] = cfg.base();
    net::reconfigure();
  } else if (cmd == "wifi_forget") {
    net::forgetWifi();
    JsonDocument e;
    e["event"] = "wifi";
    e["what"] = "forgotten";
    e["src"] = src;
    emitEvent(e);
  } else if (cmd == "softap_on") {
    net::startAp(in["minutes"] | 15);
  } else if (cmd == "set_seq") {
    // Zone turns (leader node): {"turns":[["WM2_A98803E8:1","WM2_0D4D4FB0:2"],["WM2_...:1"]]}
    if (!zone::setTurns(in["turns"], in["ips"], err)) ok = false;
    else {
      saveCfg = true;
      if (cfg.seqTurns.length() && !cfg.core.psFitted) data["warning"] = "this node has no pressure switch: water is judged from flow";
    }
  } else if (cmd == "zone_leave") {
    zone::leave();
  } else if (cmd == "identify") {
    // Blink every LED together for a while so the user can see which board this is.
    uint32_t sec = constrain((int)(in["seconds"] | 20), 0, 120);
    hw::ledsIdentify(sec);
    Serial.printf("[identify] blinking all LEDs for %lu s\n", (unsigned long)sec);
    data["seconds"] = sec;
  } else if (cmd == "bench") {
    // {"on":true,"minutes":60}: valves switch without 12V for bench checks.
    bool on = in["on"] | true;
    uint32_t m = constrain((int)(in["minutes"] | 60), 1, 240);
    hw::benchSet(on ? m : 0);
    if (on) hw::valveResync();  // re-send the current valve states as pulses
    Serial.printf("[bench] mode %s\n", on ? "ON: valves switch without 12V, no battery safe-close" : "off");
    data["bench_s"] = hw::benchLeftS();
  } else if (cmd == "valve_test") {
    int v = valveArg(in);
    if (v < 0) { ok = false; err = "valve must be 1 or 2"; }
    else if (hw::battVolts() < 6.5f && !hw::benchLeftS()) { ok = false; err = "needs 12V on J6 (or bench mode)"; }
    else {
      hw::valvePulseRaw(v, true);
      delay((uint32_t)(cfg.gapS * 1000));
      hw::valvePulseRaw(v, false);
      hw::valveRequest(v, false);
    }
  } else if (cmd == "diag_inputs") {
    // Raw look at the flow and pressure pins: sample for `ms` (default 1000)
    // and count falling edges and time spent LOW, independent of the PCNT.
    uint32_t ms = constrain((int)(in["ms"] | 1000), 100, 5000);
    const uint8_t pins[5] = {wm2::PIN_FLOW[0], wm2::PIN_FLOW[1], wm2::PIN_FLOW[2], wm2::PIN_FLOW[3], wm2::PIN_PS};
    uint32_t falls[5] = {0}, lows[5] = {0}, samples = 0;
    uint32_t pc0[4];
    for (int i = 0; i < 4; i++) pc0[i] = hw::flowPulses(i);
    bool last[5];
    for (int i = 0; i < 5; i++) last[i] = digitalRead(pins[i]);
    uint32_t t0 = micros();
    while (micros() - t0 < ms * 1000) {
      for (int i = 0; i < 5; i++) {
        bool v = digitalRead(pins[i]);
        if (!v) lows[i]++;
        if (last[i] && !v) falls[i]++;
        last[i] = v;
      }
      samples++;
      delayMicroseconds(40);
    }
    JsonArray a = data["inputs"].to<JsonArray>();
    const char* names[5] = {"flow1", "flow2", "flow3", "flow4", "ps"};
    for (int i = 0; i < 5; i++) {
      JsonObject o = a.add<JsonObject>();
      o["name"] = names[i];
      o["gpio"] = pins[i];
      o["level_now"] = digitalRead(pins[i]);
      o["pin_falls"] = falls[i];
      o["low_pct"] = samples ? roundf(lows[i] * 1000.0f / samples) / 10 : 0;
      if (i < 4) o["pcnt_counts"] = hw::flowPulses(i) - pc0[i];
    }
    data["ms"] = ms;
    data["samples"] = samples;
    data["batt_pin_mv"] = roundf(hw::battPinMv());
    data["batt_over_range"] = hw::battOverRange();
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
      cmd != "device_info" && cmd != "get_days" && cmd != "get_program" && cmd != "wifi_scan" && cmd != "diag_inputs")
    stateChanged();
  String s;
  serializeJson(resp, s);
  return s;
}
