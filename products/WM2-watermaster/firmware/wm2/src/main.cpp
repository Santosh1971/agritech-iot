// WM2-WaterMaster main firmware (Girish Farm), phase 2 "node core".
// Spec: Claude Doc "WM2 WaterMaster: Girish Farm Functional Specification".
//
// Every second: read sensors -> irrigation logic (lib/core) -> valve pulses,
// events, logging. Reports over MQTT (Girish's broker), the local web page /
// app (WebSocket), SoftAP and the serial console, all with one command set.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include "app.h"
#include "commands.h"
#include "history.h"
#include "hw.h"
#include "net.h"
#include "settings.h"
#include "timeutil.h"
#include "web.h"
#include "wm2_pins.h"

using namespace wm2core;

wm2core::Core core;
SemaphoreHandle_t gLock;

static Preferences prefs;

// ---------------------------------------------------------------- time
static uint32_t baseUtc = 0, baseMs = 0;
static bool clockOk = false;

uint32_t nowUtc() { return baseUtc + (millis() - baseMs) / 1000; }
bool timeValid() { return clockOk; }
uint32_t localEpoch() { return nowUtc() + cfg.tzMin * 60; }
uint32_t todayIndex() { return Core::localDay(localEpoch(), cfg.core.dayStartHour); }

void setTimeUtc(uint32_t utc, const char* source) {
  uint32_t before = nowUtc();
  baseUtc = utc;
  baseMs = millis();
  bool wasOk = clockOk;
  clockOk = true;
  hw::rtcWrite(utc);
  int32_t drift = (int32_t)(utc - before);
  if (!wasOk || abs(drift) > 2)
    Serial.printf("[time] set from %s: %s local (moved %ld s)\n", source, fmtLocal(localEpoch()).c_str(), (long)drift);
}

// ---------------------------------------------------------------- bypass
static bool bypass = false;
static uint32_t bypassUntil = 0;  // millis

bool bypassOn() { return bypass; }
uint32_t bypassLeftS() { return bypass ? (uint32_t)((int32_t)(bypassUntil - millis()) > 0 ? (bypassUntil - millis()) / 1000 : 0) : 0; }

void setBypass(bool on, uint32_t minutes, const char* src) {
  if (minutes == 0) minutes = cfg.bypassDefaultMin;
  minutes = constrain(minutes, 1u, 1440u);
  bool changed = on != bypass;
  bypass = on;
  bypassUntil = millis() + minutes * 60000UL;
  if (changed || on) {
    JsonDocument d;
    d["event"] = "bypass";
    d["on"] = on;
    if (on) d["minutes"] = minutes;
    d["src"] = src;
    emitEvent(d);
  }
}

// ---------------------------------------------------------------- sources
static String lastSrc[2];
static uint32_t lastSrcAt[2] = {0, 0};
void noteSource(int v, const char* src) {
  if (v < 0 || v > 1) return;
  lastSrc[v] = src;
  lastSrcAt[v] = millis();
}

// ---------------------------------------------------------------- live data
static float lpm[4] = {0, 0, 0, 0};
static float litresToday[4] = {0, 0, 0, 0};
static uint32_t litresDay = 0;
static bool ps = false;
static uint32_t lastActiveMs = 0;
static bool stateDirty = true;
static uint32_t rebootAt = 0;

static bool isActive() {
  return core.water() || hw::valveState(0) || hw::valveState(1) ||
         millis() - lastActiveMs < (uint32_t)cfg.idleDelayS * 1000;
}

void stateChanged() { stateDirty = true; }
void requestReboot(uint32_t ms) { rebootAt = millis() + ms; }

void applyCoreConfig() {
  core.cfg = cfg.core;
}

void flowStatus(JsonArray out) {
  for (int i = 0; i < 4; i++) {
    JsonObject f = out.add<JsonObject>();
    f["in"] = i + 1;
    f["valve"] = cfg.core.flowValve[i] + 1;
    f["lpm"] = roundf(lpm[i] * 10) / 10;
    f["l_today"] = roundf(litresToday[i] * 10) / 10;
    f["l_total"] = roundf(hw::flowTotal(i) * 10) / 10;
  }
}

void buildState(JsonObject o) {
  o["v"] = 1;
  o["ts"] = nowUtc();
  o["time_ok"] = clockOk;
  if (clockOk) o["local"] = fmtLocal(localEpoch());
  o["node"] = cfg.zone + "/" + cfg.node;
  o["fw"] = FW_VERSION;
  o["state"] = isActive() ? "active" : "idle";
  o["water"] = core.water();
  o["ps"] = ps;
  JsonObject b = o["bypass"].to<JsonObject>();
  b["on"] = bypass;
  b["left_s"] = bypassLeftS();
  flowStatus(o["flow"].to<JsonArray>());
  JsonArray va = o["valves"].to<JsonArray>();
  for (int v = 0; v < 2; v++) {
    const ValveState& s = core.valve(v);
    const ValveCfg& c = cfg.core.v[v];
    JsonObject j = va.add<JsonObject>();
    j["v"] = v + 1;
    j["open"] = hw::valveState(v);
    j["target"] = s.open;
    j["pending"] = hw::valvePending(v);
    j["waiting"] = s.waiting;
    j["mode"] = modeName(c.mode);
    j["due"] = s.due;
    j["done"] = s.done;
    if (c.prog.type == PROG_LITRES) j["planned_l"] = c.prog.litres + (uint32_t)s.carryL;
    else j["planned_min"] = c.prog.enabled ? c.prog.minutes + s.carrySec / 60 : 0;
    j["run_min"] = s.runSec / 60;
    float l = 0;
    for (int i = 0; i < 4; i++)
      if (cfg.core.flowValve[i] == v) l += litresToday[i];
    j["litres"] = roundf(l * 10) / 10;
    j["manual"] = s.manual;
    if (s.manual) {
      j["manual_left_s"] = s.manualLeftSec;
      j["manual_left_l"] = s.manualLeftL;
    }
    j["never_close"] = c.neverClose;
  }
  JsonObject bt = o["batt"].to<JsonObject>();
  bt["v"] = roundf(hw::battVolts() * 100) / 100;
  bt["soc"] = hw::battSoc();
  bt["trend"] = hw::battTrend();
  bt["days"] = roundf(hw::battDaysLeft() * 10) / 10;
  bt["level"] = hw::battLevel();
  JsonObject w = o["wifi"].to<JsonObject>();
  w["ip"] = net::ip();
  w["rssi"] = net::rssi();
  w["ap"] = net::apOn();
  w["ap_clients"] = net::apClients();
  o["mqtt"] = net::mqttUp();
  JsonArray al = o["alarms"].to<JsonArray>();
  for (int a = 0; a < A_COUNT; a++)
    if (core.alarm((AlarmCode)a)) al.add(alarmName(a));
  o["uptime_s"] = millis() / 1000;
}

void emitEvent(JsonDocument& d) {
  d["v"] = 1;
  d["ts"] = nowUtc();
  d["node"] = cfg.zone + "/" + cfg.node;
  String line;
  serializeJson(d, line);
  if (clockOk) history::addEvent(line);
  net::publish("event", line);
  web::broadcast("{\"type\":\"event\",\"data\":" + line + "}");
  Serial.println("[event] " + line);
  stateDirty = true;
}

// ---------------------------------------------------------------- progress persistence
static uint32_t savedRun[2] = {0, 0};

void saveProgressNow() {
  prefs.begin("prog", false);
  prefs.putULong("day", core.dayIndex());
  prefs.putULong("r0", core.valve(0).runSec);
  prefs.putULong("r1", core.valve(1).runSec);
  prefs.putFloat("l0", core.valve(0).runL);
  prefs.putFloat("l1", core.valve(1).runL);
  for (int i = 0; i < 4; i++) {
    char k[4] = {'n', (char)('0' + i), 0};
    prefs.putFloat(k, core.normalLpm(i));
    k[0] = 's';
    prefs.putUChar(k, core.normalSamples(i));
  }
  prefs.end();
  savedRun[0] = core.valve(0).runSec;
  savedRun[1] = core.valve(1).runSec;
}

static void restoreProgress() {
  prefs.begin("prog", true);
  uint32_t day = prefs.getULong("day", 0);
  for (int i = 0; i < 4; i++) {
    char k[4] = {'n', (char)('0' + i), 0};
    float n = prefs.getFloat(k, 0);
    k[0] = 's';
    core.setNormal(i, n, prefs.getUChar(k, 0));
  }
  if (clockOk && day && (day == todayIndex() || day + 1 == todayIndex())) {
    core.restoreProgress(0, day, prefs.getULong("r0", 0), prefs.getFloat("l0", 0));
    core.restoreProgress(1, day, prefs.getULong("r1", 0), prefs.getFloat("l1", 0));
    Serial.printf("[prog] restored today's progress: V1 %lu s, V2 %lu s\n", (unsigned long)core.valve(0).runSec,
                  (unsigned long)core.valve(1).runSec);
  }
  prefs.end();
}

// ---------------------------------------------------------------- events from the logic
static void handleCoreEvents(int n) {
  for (int i = 0; i < n; i++) {
    const Event& e = core.events()[i];
    JsonDocument d;
    switch (e.type) {
      case EV_VALVE:
        d["event"] = "valve";
        d["valve"] = e.valve + 1;
        d["open"] = e.a == 1;
        d["reason"] = reasonName(e.reason);
        if (millis() - lastSrcAt[e.valve] < 15000) d["src"] = lastSrc[e.valve];
        break;
      case EV_WATER:
        d["event"] = "water";
        d["present"] = e.a == 1;
        break;
      case EV_MANUAL_DONE:
        d["event"] = "manual_done";
        d["valve"] = e.valve + 1;
        break;
      case EV_ALARM:
        d["event"] = "alarm";
        d["code"] = alarmName(e.reason);
        if (e.valve >= 0) d["valve"] = e.valve + 1;
        d["active"] = e.a == 1;
        break;
      case EV_DAY_RESULT: {
        const DayResult& r = e.day;
        d["event"] = "day_result";
        d["valve"] = e.valve + 1;
        d["day"] = r.day;
        d["date"] = dateOfDay(r.day);
        if (r.plannedL) d["planned_l"] = r.plannedL;
        else d["planned_min"] = r.plannedMin;
        d["run_min"] = r.runMin;
        JsonArray l = d["litres"].to<JsonArray>();
        l.add(roundf(r.litres[0] * 10) / 10);
        l.add(roundf(r.litres[1] * 10) / 10);
        JsonArray nl = d["normal_litres"].to<JsonArray>();
        nl.add(roundf(r.normalL[0]));
        nl.add(roundf(r.normalL[1]));
        d["water_min"] = r.waterMin;
        d["status"] = r.status == 0 ? "done" : r.status == 1 ? "partial" : "no_water";
        JsonArray al = d["alarms"].to<JsonArray>();
        for (int a = 0; a < A_COUNT; a++)
          if (core.alarm((AlarmCode)a)) al.add(alarmName(a));
        String line;
        serializeJson(d, line);
        history::addDay(line);
        break;
      }
      default:
        continue;
    }
    emitEvent(d);
  }
}

// ---------------------------------------------------------------- records, reporting, backlog
static float accL[4] = {0, 0, 0, 0};
static uint32_t lastRecMs = 0, lastRecUtc = 0;
static uint32_t lastSentUtc = 0;
static bool backlog = false;
static uint32_t lastSentSaved = 0;

static String recJson(const Rec& r, bool backfill) {
  JsonDocument d;
  d["v"] = 1;
  d["ts"] = r.ts;
  d["node"] = cfg.zone + "/" + cfg.node;
  if (backfill) d["backfill"] = true;
  d["dt"] = r.dt;
  d["ps"] = (r.flags & 1) != 0;
  d["water"] = (r.flags & 2) != 0;
  JsonArray v = d["valves"].to<JsonArray>();
  v.add((r.flags & 4) != 0);
  v.add((r.flags & 8) != 0);
  d["bypass"] = (r.flags & 16) != 0;
  JsonArray lp = d["lpm"].to<JsonArray>();
  JsonArray l = d["l"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    lp.add(r.lpm10[i] / 10.0f);
    l.add(r.l10[i] / 10.0f);
  }
  d["batt"] = r.battCv / 100.0f;
  String s;
  serializeJson(d, s);
  return s;
}

static void makeRecord(bool active) {
  Rec r = {};
  uint32_t now = nowUtc();
  r.ts = now;
  r.dt = (uint16_t)min<uint32_t>(lastRecUtc ? now - lastRecUtc : (millis() - lastRecMs) / 1000, 65535);
  r.flags = (ps ? 1 : 0) | (core.water() ? 2 : 0) | (hw::valveState(0) ? 4 : 0) | (hw::valveState(1) ? 8 : 0) |
            (bypass ? 16 : 0) | (active ? 32 : 0);
  r.rssi = (int8_t)net::rssi();
  r.battCv = (uint16_t)(hw::battVolts() * 100);
  for (int i = 0; i < 4; i++) {
    r.lpm10[i] = (uint16_t)min(lpm[i] * 10, 65535.0f);
    r.l10[i] = (uint16_t)min(accL[i] * 10 + 0.5f, 65535.0f);
    accL[i] = 0;
  }
  lastRecUtc = now;
  if (clockOk) history::add(r, Core::localDay(now + cfg.tzMin * 60, 0));

  bool sent;
  if (active) {
    JsonDocument d;
    buildState(d.to<JsonObject>());
    String s;
    serializeJson(d, s);
    sent = net::publish("telemetry", s);
    net::publish("state", s, true);
  } else {
    JsonDocument d;
    d["v"] = 1;
    d["ts"] = now;
    d["node"] = cfg.zone + "/" + cfg.node;
    d["fw"] = FW_VERSION;
    JsonObject b = d["batt"].to<JsonObject>();
    b["v"] = roundf(hw::battVolts() * 100) / 100;
    b["soc"] = hw::battSoc();
    b["trend"] = hw::battTrend();
    b["days"] = roundf(hw::battDaysLeft() * 10) / 10;
    d["rssi"] = net::rssi();
    JsonArray v = d["valves"].to<JsonArray>();
    v.add(hw::valveState(0));
    v.add(hw::valveState(1));
    String s;
    serializeJson(d, s);
    sent = net::publish("heartbeat", s);
  }
  if (sent && clockOk && !backlog) lastSentUtc = now;
  if (!sent && clockOk) backlog = true;
}

static void drainBacklog() {
  if (!backlog || !net::mqttUp() || !clockOk) return;
  Rec buf[5];
  int n = history::since(lastSentUtc, buf, 5);
  for (int i = 0; i < n; i++) {
    if (!net::publish("telemetry", recJson(buf[i], true))) return;
    lastSentUtc = buf[i].ts;
  }
  if (n < 5) backlog = false;
}

// ---------------------------------------------------------------- serial console
static void serialConsole() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.length() < 1024) line += c;
      continue;
    }
    line.trim();
    if (line.startsWith("{")) {
      Serial.println(dispatchCommand(line, "serial"));
    } else if (line == "s" || line == "state") {
      JsonDocument d;
      {
        Lock lock;
        buildState(d.to<JsonObject>());
      }
      serializeJsonPretty(d, Serial);
      Serial.println();
    } else if (line.length()) {
      Serial.println(
          "WM2 console. Type a JSON command or 's' for state. Examples:\n"
          " {\"cmd\":\"wifi_config\",\"ssid\":\"FarmWiFi\",\"pass\":\"secret\"}\n"
          " {\"cmd\":\"mqtt_config\",\"host\":\"broker.example.com\",\"port\":1883,\"zone\":\"z1\",\"node\":\"n1\"}\n"
          " {\"cmd\":\"rtc_sync\",\"epoch\":1793083200}\n"
          " {\"cmd\":\"valve_open\",\"valve\":1,\"minutes\":2}   {\"cmd\":\"valve_close\",\"valve\":1}\n"
          " {\"cmd\":\"bypass\",\"on\":true,\"minutes\":10}\n"
          " {\"cmd\":\"set_program\",\"valve\":1,\"program\":{\"days\":[\"mon\",\"wed\",\"fri\"],\"minutes\":45}}\n"
          " {\"cmd\":\"get_config\"}  {\"cmd\":\"device_info\"}  {\"cmd\":\"get_events\",\"limit\":10}");
    }
    line = "";
  }
}

// ---------------------------------------------------------------- setup / loop
static bool doubleResetCheck() {
  prefs.begin("boot", false);
  bool recent = prefs.getBool("recent", false);
  prefs.putBool("recent", true);
  prefs.end();
  return recent;
}

void setup() {
  hw::valvesBegin();  // drivers safe before anything else
  Serial.begin(115200);
  delay(200);
  gLock = xSemaphoreCreateRecursiveMutex();
  Serial.printf("\n\n=== WM2 WaterMaster fw %s ===\n", FW_VERSION);

  bool dbl = doubleResetCheck();
  history::begin();
  if (!cfg.load()) {
    Serial.println("[cfg] no saved settings, using defaults");
    cfg.save();
  }
  hw::ledsBegin();
  hw::flowBegin();
  hw::psBegin();
  analogSetPinAttenuation(wm2::PIN_BATT_ADC, ADC_11db);
  for (int i = 0; i < 5; i++) hw::battRead();

  if (hw::rtcBegin()) {
    uint32_t t;
    if (hw::rtcRead(t)) {
      baseUtc = t;
      baseMs = millis();
      clockOk = true;
      Serial.printf("[time] RTC: %s local\n", fmtLocal(localEpoch()).c_str());
    } else {
      Serial.println("[time] RTC not set: programs wait until time is synced (NTP, app or rtc_sync)");
    }
  } else {
    Serial.println("[time] RTC not found");
  }

  applyCoreConfig();
  restoreProgress();
  ps = hw::psState();

  prefs.begin("net", true);
  lastSentUtc = prefs.getULong("sent", 0);
  prefs.end();
  if (lastSentUtc == 0 && clockOk) lastSentUtc = nowUtc();
  lastSentSaved = lastSentUtc;
  backlog = true;  // send anything logged while offline before the reboot

  net::begin(dbl);
  web::begin();
  hw::valveResync();  // latching valves can't report position: repeat the last pulse

  Serial.printf("[cfg] node %s, topics %s..., SoftAP WM2-%s-%s / %s\n", cfg.hostname().c_str(), cfg.base().c_str(),
                cfg.zone.c_str(), cfg.node.c_str(), cfg.apPassword().c_str());
  Serial.println("Type 'h' + Enter for console help.");
  lastRecMs = millis();
}

void loop() {
  static uint32_t lastTick = 0, lastLed = 0, lastPush = 0, lastWsPush = 0, lastSave = 0, lastHouse = 0,
                  lastRtc = 0, lastStatus = 0, lastDrain = 0;
  static const char* lastLevel = "";
  static bool bootFlagCleared = false;
  uint32_t now = millis();

  net::loop();
  web::loop();
  hw::valvesLoop();
  serialConsole();

  if (!bootFlagCleared && now > 5000) {
    prefs.begin("boot", false);
    prefs.putBool("recent", false);
    prefs.end();
    bootFlagCleared = true;
  }

  if (now - lastTick >= 1000) {
    Lock lock;
    uint32_t dtMs = lastTick ? now - lastTick : 1000;
    lastTick = now;
    hw::battRead();
    float dl[4];
    hw::flowSample(lpm, dl);
    ps = hw::psState();
    if (bypass && (int32_t)(bypassUntil - now) <= 0) setBypass(false, 0, "timer");

    Inputs in;
    in.localEpoch = localEpoch();
    in.timeValid = clockOk;
    in.ps = ps;
    in.bypass = bypass;
    in.dtMs = dtMs;
    for (int i = 0; i < 4; i++) {
      in.lpm[i] = lpm[i];
      in.litresDelta[i] = dl[i];
    }
    uint32_t dayBefore = core.dayIndex();
    int n = core.tick(in);
    handleCoreEvents(n);
    if (core.dayIndex() != dayBefore || litresDay != core.dayIndex()) {
      for (int i = 0; i < 4; i++) litresToday[i] = 0;
      litresDay = core.dayIndex();
    }
    bool anyFlow = false;
    for (int i = 0; i < 4; i++) {
      accL[i] += dl[i];
      litresToday[i] += dl[i];
      if (lpm[i] > cfg.core.flowSeenLpm) anyFlow = true;
    }
    if (core.water() || anyFlow || hw::valveState(0) || hw::valveState(1)) lastActiveMs = now;

    // Valves follow the logic, except at critical battery (safe state).
    bool critical = !strcmp(hw::battLevel(), "critical");
    for (int v = 0; v < 2; v++) {
      bool want = core.valve(v).open;
      if (critical && cfg.safeClose && !cfg.core.v[v].neverClose) want = false;
      hw::valveRequest(v, want);
    }

    const char* lvl = hw::battLevel();
    if (strcmp(lvl, lastLevel) != 0) {
      if (*lastLevel) {
        JsonDocument d;
        d["event"] = "battery";
        d["level"] = lvl;
        d["v"] = roundf(hw::battVolts() * 100) / 100;
        emitEvent(d);
      }
      lastLevel = lvl;
    }

    bool active = isActive();
    uint32_t interval = (active ? cfg.activeS : cfg.heartbeatS) * 1000UL;
    if (now - lastRecMs >= interval) {
      lastRecMs = now;
      makeRecord(active);
    }

    if (now - lastSave >= 60000) {
      lastSave = now;
      if (core.valve(0).runSec != savedRun[0] || core.valve(1).runSec != savedRun[1]) saveProgressNow();
      if (lastSentUtc != lastSentSaved) {
        prefs.begin("net", false);
        prefs.putULong("sent", lastSentUtc);
        prefs.end();
        lastSentSaved = lastSentUtc;
      }
    }
    if (now - lastHouse >= 600000) {
      lastHouse = now;
      history::housekeeping();
    }
    if (now - lastRtc >= 3600000UL) {  // keep millis-based clock aligned with the RTC
      lastRtc = now;
      uint32_t t;
      if (hw::rtcRead(t) && clockOk && abs((int32_t)(t - nowUtc())) > 2) {
        baseUtc = t;
        baseMs = millis();
      }
    }
    if (now - lastStatus >= 10000) {
      lastStatus = now;
      Serial.printf("[st] %s water=%d ps=%d bypass=%d | V1 %s%s V2 %s%s | lpm %.1f %.1f %.1f %.1f | %.2fV | wifi %s mqtt %d%s\n",
                    clockOk ? fmtLocal(localEpoch()).c_str() : "no-clock", core.water(), ps, bypass,
                    hw::valveState(0) ? "OPEN" : "closed", hw::valvePending(0) ? "(pending)" : "",
                    hw::valveState(1) ? "OPEN" : "closed", hw::valvePending(1) ? "(pending)" : "", lpm[0], lpm[1],
                    lpm[2], lpm[3], hw::battVolts(), net::wifiUp() ? net::ip().c_str() : "-", net::mqttUp(),
                    net::apOn() ? " AP" : "");
    }
  }

  if (now - lastLed >= 40) {
    lastLed = now;
    hw::ledsLoop(net::apOn() && !net::wifiUp() ? hw::WL_AP
                 : net::mqttUp()                ? hw::WL_MQTT
                 : net::wifiUp()                ? hw::WL_WIFI
                 : net::apOn()                  ? hw::WL_AP
                                                : hw::WL_OFF,
                 core.water(), bypass);
  }

  if ((stateDirty && now - lastPush >= 1000) || (web::clients() && now - lastWsPush >= 2000)) {
    JsonDocument d;
    {
      Lock lock;
      buildState(d.to<JsonObject>());
    }
    String s;
    serializeJson(d, s);
    web::broadcast("{\"type\":\"state\",\"data\":" + s + "}");
    lastWsPush = now;
    if (stateDirty) {
      net::publish("state", s, true);
      lastPush = now;
      stateDirty = false;
    }
  }

  if (now - lastDrain >= 300) {
    lastDrain = now;
    Lock lock;
    drainBacklog();
  }

  if (rebootAt && (int32_t)(now - rebootAt) > 0) ESP.restart();
}
