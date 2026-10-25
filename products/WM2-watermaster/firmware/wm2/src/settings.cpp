#include "settings.h"

#include <LittleFS.h>
#include <WiFi.h>

using namespace wm2core;

Settings cfg;

static const char* DAY_NAMES[7] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

const char* modeName(Mode m) {
  switch (m) {
    case MODE_PROGRAM: return "program";
    case MODE_MANUAL: return "manual";
    default: return "off";
  }
}

bool modeFromName(const char* s, Mode& m) {
  if (!s) return false;
  if (!strcmp(s, "program")) m = MODE_PROGRAM;
  else if (!strcmp(s, "manual")) m = MODE_MANUAL;
  else if (!strcmp(s, "off")) m = MODE_OFF;
  else return false;
  return true;
}

static String hhmm(uint16_t m) {
  char b[6];
  snprintf(b, sizeof b, "%02u:%02u", (m / 60) % 25, m % 60);
  return b;
}

static bool parseHhmm(const char* s, uint16_t& out) {
  if (!s) return false;
  int h, m;
  if (sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 24 || m < 0 || m > 59) return false;
  out = (uint16_t)(h * 60 + m);
  if (out > 1440) out = 1440;
  return true;
}

void programToJson(const Program& p, JsonObject o) {
  o["enabled"] = p.enabled;
  o["type"] = p.type == PROG_LITRES ? "litres" : "timer";
  if (p.everyDays) {
    o["every_days"] = p.everyDays;
    o["anchor_day"] = p.anchorDay;
  } else {
    JsonArray d = o["days"].to<JsonArray>();
    for (int i = 0; i < 7; i++)
      if (p.daysMask & (1 << i)) d.add(DAY_NAMES[i]);
  }
  if (p.type == PROG_LITRES) o["litres"] = p.litres;
  else o["minutes"] = p.minutes;
  JsonArray w = o["window"].to<JsonArray>();
  w.add(hhmm(p.winFrom));
  w.add(p.winTo >= 1440 ? String("23:59") : hhmm(p.winTo));
  o["carry_over"] = p.carryOver;
}

bool programFromJson(JsonObjectConst o, Program& p, uint32_t today, String& err) {
  Program n;
  n.enabled = o["enabled"] | true;
  const char* type = o["type"] | "timer";
  n.type = !strcmp(type, "litres") ? PROG_LITRES : PROG_TIMER;
  if (o["every_days"].is<int>() && (int)o["every_days"] > 0) {
    n.everyDays = (uint8_t)constrain((int)o["every_days"], 1, 30);
    n.anchorDay = o["anchor_day"] | today;
  } else {
    JsonArrayConst d = o["days"];
    if (d.isNull()) { err = "days or every_days required"; return false; }
    for (JsonVariantConst v : d) {
      const char* s = v.as<const char*>();
      bool ok = false;
      for (int i = 0; i < 7 && s; i++)
        if (!strncasecmp(s, DAY_NAMES[i], 3)) { n.daysMask |= 1 << i; ok = true; }
      if (!ok) { err = "bad day name"; return false; }
    }
  }
  if (n.type == PROG_TIMER) {
    int m = o["minutes"] | 0;
    if (m < 1 || m > 1440) { err = "minutes must be 1..1440"; return false; }
    n.minutes = (uint16_t)m;
  } else {
    long l = o["litres"] | 0;
    if (l < 1) { err = "litres must be > 0"; return false; }
    n.litres = (uint32_t)l;
  }
  JsonArrayConst w = o["window"];
  if (!w.isNull() && w.size() == 2) {
    uint16_t a, b;
    if (!parseHhmm(w[0], a) || !parseHhmm(w[1], b)) { err = "window must be [\"HH:MM\",\"HH:MM\"]"; return false; }
    n.winFrom = a;
    n.winTo = (b == 23 * 60 + 59) ? 1440 : b;
  }
  n.carryOver = o["carry_over"] | false;
  p = n;
  return true;
}

String Settings::apPassword() const {
  if (apPass.length() >= 8) return apPass;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[12];
  snprintf(b, sizeof b, "wm2-%02x%02x", mac[4], mac[5]);
  return b;
}

String Settings::hostname() const { return "wm2-" + zone + "-" + node; }
String Settings::deviceId() const {
  if (agId.length()) return agId;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[16];
  // 4-byte suffix, as WM1 (webapp lib/deviceIdentity.ts: 2 bytes collided on FG1)
  snprintf(b, sizeof b, "WM2_%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
  return b;
}

// AgriSense follows its unified convention agrisense/<product>/<deviceId>/...;
// Girish's broker uses farm/zone/node naming.
String Settings::base() const {
  if (broker == 0) return "agrisense/WM2/" + deviceId() + "/";
  return farm + "/wm2/" + zone + "/" + node + "/";
}

void Settings::toJson(JsonObject o, bool secrets) const {
  o["farm"] = farm;
  o["zone"] = zone;
  o["node"] = node;
  o["ssid"] = ssid;
  if (secrets) o["pass"] = pass;
  o["broker"] = brokerName();
  o["ag_host"] = agHost;
  o["ag_port"] = agPort;
  o["ag_user"] = agUser;
  o["ag_id"] = deviceId();
  if (secrets) o["ag_pass"] = agPass;
  o["mqtt_host"] = mqttHost;
  o["mqtt_port"] = mqttPort;
  o["mqtt_user"] = mqttUser;
  if (secrets) {
    o["mqtt_pass"] = mqttPass;
    o["ap_pass"] = apPass;
  }
  o["tz_min"] = tzMin;
  o["active_s"] = activeS;
  o["heartbeat_s"] = heartbeatS;
  o["idle_delay_s"] = idleDelayS;
  o["ps_settle_s"] = psSettleS;
  o["profile"] = profile == 1 ? "saver" : profile == 2 ? "auto" : "responsive";
  o["check_min"] = checkMin;
  o["gap_s"] = gapS;
  o["safe_close"] = safeClose;
  o["bypass_default_min"] = bypassDefaultMin;
  o["chemistry"] = chem == 1 ? "lead-acid" : "lifepo4";
  o["capacity_ah"] = capAh;
  o["v_warn"] = vWarn;
  o["v_save"] = vSave;
  o["v_crit"] = vCrit;
  o["daily_ah"] = dailyAh;
  o["sequential"] = core.sequential;
  o["day_start_hour"] = core.dayStartHour;
  o["low_pct"] = core.lowPct;
  o["high_pct"] = core.highPct;
  o["imbalance_pct"] = core.imbalancePct;
  o["alarm_delay_s"] = core.alarmDelayS;
  JsonArray fl = o["flow"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject f = fl.add<JsonObject>();
    f["input"] = i + 1;
    f["valve"] = core.flowValve[i] + 1;  // 0 = unused
    f["ppl"] = ppl[i];
  }
  JsonArray va = o["valves"].to<JsonArray>();
  for (int v = 0; v < 2; v++) {
    JsonObject j = va.add<JsonObject>();
    const ValveCfg& c = core.v[v];
    j["valve"] = v + 1;
    j["mode"] = modeName(c.mode);
    j["pulse_ms"] = pulseMs[v];
    j["invert"] = invert[v];
    j["never_close"] = c.neverClose;
    j["on_water_loss"] = c.closeOnWaterLoss ? "close" : "keep";
    j["close_after_min"] = c.waterLossCloseMin;
    programToJson(c.prog, j["program"].to<JsonObject>());
  }
}

template <typename T>
static void take(JsonObjectConst o, const char* k, T& dst) {
  if (!o[k].isNull()) dst = o[k].as<T>();
}

void Settings::fromJson(JsonObjectConst o) {
  take(o, "farm", farm);
  take(o, "zone", zone);
  take(o, "node", node);
  take(o, "ssid", ssid);
  take(o, "pass", pass);
  if (!o["broker"].isNull()) broker = !strcmp(o["broker"] | "agrisense", "girish") ? 1 : 0;
  take(o, "ag_host", agHost);
  take(o, "ag_port", agPort);
  take(o, "ag_user", agUser);
  take(o, "ag_pass", agPass);
  if (!o["ag_id"].isNull()) {
    String v = o["ag_id"].as<String>();
    agId = "";
    agId = v == deviceId() ? String("") : v;  // same as the MAC-based ID: stay automatic
  }
  take(o, "mqtt_host", mqttHost);
  take(o, "mqtt_port", mqttPort);
  take(o, "mqtt_user", mqttUser);
  take(o, "mqtt_pass", mqttPass);
  take(o, "ap_pass", apPass);
  take(o, "tz_min", tzMin);
  take(o, "active_s", activeS);
  take(o, "heartbeat_s", heartbeatS);
  take(o, "idle_delay_s", idleDelayS);
  take(o, "ps_settle_s", psSettleS);
  if (!o["profile"].isNull()) {
    const char* p = o["profile"] | "responsive";
    profile = !strcmp(p, "saver") ? 1 : !strcmp(p, "auto") ? 2 : 0;
  }
  take(o, "check_min", checkMin);
  take(o, "gap_s", gapS);
  take(o, "safe_close", safeClose);
  take(o, "bypass_default_min", bypassDefaultMin);
  if (!o["chemistry"].isNull()) chem = !strcmp(o["chemistry"] | "", "lead-acid") ? 1 : 0;
  take(o, "capacity_ah", capAh);
  take(o, "v_warn", vWarn);
  take(o, "v_save", vSave);
  take(o, "v_crit", vCrit);
  take(o, "daily_ah", dailyAh);
  take(o, "sequential", core.sequential);
  take(o, "day_start_hour", core.dayStartHour);
  take(o, "low_pct", core.lowPct);
  take(o, "high_pct", core.highPct);
  take(o, "imbalance_pct", core.imbalancePct);
  take(o, "alarm_delay_s", core.alarmDelayS);
  for (JsonObjectConst f : o["flow"].as<JsonArrayConst>()) {
    int i = (f["input"] | 0) - 1;
    if (i < 0 || i > 3) continue;
    if (!f["valve"].isNull()) core.flowValve[i] = (int8_t)((f["valve"] | 0) - 1);
    if (!f["ppl"].isNull()) ppl[i] = f["ppl"] | 450.0f;
  }
  for (JsonObjectConst j : o["valves"].as<JsonArrayConst>()) {
    int v = (j["valve"] | 0) - 1;
    if (v < 0 || v > 1) continue;
    ValveCfg& c = core.v[v];
    if (!j["mode"].isNull()) modeFromName(j["mode"], c.mode);
    take(j, "pulse_ms", pulseMs[v]);
    take(j, "invert", invert[v]);
    take(j, "never_close", c.neverClose);
    if (!j["on_water_loss"].isNull()) c.closeOnWaterLoss = !strcmp(j["on_water_loss"] | "keep", "close");
    take(j, "close_after_min", c.waterLossCloseMin);
    if (j["program"].is<JsonObjectConst>()) {
      String err;
      programFromJson(j["program"], c.prog, 0, err);
    }
  }
  activeS = constrain(activeS, 30, 1800);
  heartbeatS = constrain(heartbeatS, 60, 14400);
  for (int v = 0; v < 2; v++) pulseMs[v] = constrain(pulseMs[v], 10, 500);
  gapS = constrain(gapS, 1.0f, 5.0f);
}

bool Settings::load() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) return false;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return false;
  fromJson(d.as<JsonObjectConst>());
  return true;
}

bool Settings::save() const {
  JsonDocument d;
  toJson(d.to<JsonObject>(), true);
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  LittleFS.remove("/config.json");
  return LittleFS.rename("/config.tmp", "/config.json");
}
