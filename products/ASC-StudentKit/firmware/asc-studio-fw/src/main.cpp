// ASC Studio firmware: one image for every ASC Student Kit.
//
// At boot it works out which board it is on (BOARD_ID divider, HW-12), loads
// the student's design from flash, and runs the design's rules once a second.
// The studio (USB serial) and the ASC Studio app (Bluetooth LE) both talk to
// it with one JSON object per line; the protocol is documented in ../README.md
// and mirrored by the web app's app/studio/[id]/device.ts and the app's
// lib/board.dart. Every few minutes it appends the readings to the field log.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <RTClib.h>
#include <Wire.h>

#include "blocks.h"
#include "board_map.h"
#include "design.h"
#include "fieldlog.h"
#include "link_ble.h"
#include "rules.h"
#ifdef ASC_STANDIN_GPSIOAM
#include "standin_gpsioam.h"
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

static const BoardMap* board = &BOARDS[0];
static bool standIn = false;  // no BOARD_ID divider: an ESP32-S3 DevKit wired to the Mini pin map
static Design design;
static bool haveDesign = false;
static Preferences prefs;
static RTC_DS3231 rtc;
static bool haveRtc = false;

// Wall clock: from the RTC when there is one, else from the studio's "time" command.
static uint32_t clockUnix = 0;
static uint32_t clockAtMs = 0;

// Outputs: state, and a manual override set from the studio or the app (-1 = rules decide).
static const int MAX_OUT = 4;
static bool outOn[MAX_OUT];
static int8_t manual[MAX_OUT];
static uint32_t manualUntilMs[MAX_OUT];

// Where a command came from, so its reply (and live frames) go back there.
enum Link : uint8_t { LINK_SERIAL, LINK_BLE };
static Link replyTo = LINK_SERIAL;
static bool liveOn[2] = {false, false};
static uint32_t lastTickMs = 0;
static String line;

// Changes over Bluetooth (a new design, switching outputs) need the PAIR
// button pressed within the last two minutes, so a stranger nearby can't
// take control (SW-10). USB needs no button: it needs the cable.
static uint32_t bleUnlockedUntilMs = 0;
static const uint32_t BLE_UNLOCK_MS = 2UL * 60 * 1000;

// Field log: one record every logEveryMin minutes once the time is known.
static uint16_t logEveryMin = 10;
static uint32_t lastLogMinute = 0;

// ---------- helpers ----------

static void sendTo(Link link, JsonDocument& doc) {
  if (link == LINK_SERIAL) {
    serializeJson(doc, Serial);
    Serial.println();
  } else {
    String s;
    serializeJson(doc, s);
    ble::sendLine(s);
  }
}

static void send(JsonDocument& doc) { sendTo(replyTo, doc); }

static void reply(const char* type, bool ok, const String& error = "") {
  JsonDocument doc;
  doc["type"] = type;
  doc["ok"] = ok;
  if (error.length()) doc["error"] = error;
  send(doc);
}

static uint32_t nowUnix() {
  if (clockUnix == 0) return 0;
  return clockUnix + (millis() - clockAtMs) / 1000;
}

static int minuteOfDay() {
  uint32_t t = nowUnix();
  if (t == 0) return -1;
  int32_t local = (int32_t)(t % 86400) + (haveDesign ? design.tzOffsetMin : 330) * 60;
  local = ((local % 86400) + 86400) % 86400;
  return local / 60;
}

static int outIndex(const char* port) {
  int n = 0;
  if (sscanf(port, "OUT%d", &n) == 1 && n >= 1 && n <= board->relays) return n - 1;
  return -1;
}

static void setOutput(int i, bool on) {
  outOn[i] = on;
  if (board->out[i] >= 0) digitalWrite(board->out[i], on ? HIGH : LOW);
}

static void allOutputsOff() {
  for (int i = 0; i < board->relays; i++) { manual[i] = -1; setOutput(i, false); }
}

static void beep(int ms) {
  if (board->buzzer < 0) return;
  digitalWrite(board->buzzer, HIGH);
  delay(ms);
  digitalWrite(board->buzzer, LOW);
}

static String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "ASC-%s-%04X", board->name, (unsigned)((mac >> 32) & 0xFFFF));
  String s(buf);
  s.toUpperCase();
  return s;
}

// ---------- board and design ----------

static void detectBoard() {
#ifdef ASC_STANDIN_GPSIOAM
  board = &GPSIOAM_BOARD;
  standIn = true;
  return;
#endif
  // The weak internal pull-down holds a missing divider (the DevKit stand-in)
  // near 0 V; a real divider (tens of kΩ) easily overrides it.
  pinMode(BOARD_ID_PIN, INPUT_PULLDOWN);
  analogSetPinAttenuation(BOARD_ID_PIN, ADC_11db);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BOARD_ID_PIN);
  float ratio = (sum / 8.0f) / 3300.0f;
  if (fabsf(ratio - BOARD_ID_MEGA) < 0.1f) board = &BOARDS[1];
  else board = &BOARDS[0];
  standIn = fabsf(ratio - BOARD_ID_MINI) >= 0.1f && fabsf(ratio - BOARD_ID_MEGA) >= 0.1f;
}

static bool applyDesign(const String& json, String& error) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) { error = "The design could not be read."; return false; }
  Design next;
  if (!parseDesign(doc.as<JsonVariantConst>(), *board, next, error)) return false;
  allOutputsOff();
  design = next;
  haveDesign = true;
  logEveryMin = constrain((int)(doc["logEveryMin"] | 10), 1, 60);
  blocksBegin(design, *board);
  return true;
}

static void fillValues(JsonObject values) {
  for (int s = 0; s < design.nSlots; s++) {
    const Slot& slot = design.slots[s];
    for (int v = 0; v < slot.nValues; v++) {
      String key = String(slot.port) + (slot.valueKeys[v][0] ? String(":") + slot.valueKeys[v] : "");
      if (isnan(slot.values[v])) values[key] = nullptr;
      else values[key] = roundf(slot.values[v] * 10) / 10;
    }
  }
}

static void logRecord() {
  uint32_t t = nowUnix();
  if (t == 0 || !haveDesign) return;  // records need a real time
  uint32_t minute = t / 60;
  if (minute == lastLogMinute || minute % logEveryMin != 0) return;
  lastLogMinute = minute;
  JsonDocument rec;
  rec["t"] = t;  // must stay first: fieldlog reads it without parsing
  rec["d"] = design.version;
  fillValues(rec["v"].to<JsonObject>());
  JsonObject o = rec["o"].to<JsonObject>();
  for (int i = 0; i < board->relays; i++) o[String("OUT") + (i + 1)] = outOn[i] ? 1 : 0;
  String s;
  serializeJson(rec, s);
  fieldlog::append(s);
}

// ---------- the once-a-second tick ----------

static void tick() {
  if (!haveDesign) return;
  blocksRead(design);
  int mod = minuteOfDay();
  uint32_t now = millis();

  for (int i = 0; i < board->relays; i++) {
    if (manual[i] >= 0 && (int32_t)(now - manualUntilMs[i]) > 0) manual[i] = -1;  // manual control times out
  }
  for (int r = 0; r < design.nRules; r++) {
    const Rule& rule = design.rules[r];
    int i = outIndex(rule.out);
    if (i < 0 || manual[i] >= 0) continue;
    float v = valueFor(design, rule.sensor);
    int g = rule.guard[0] ? guardOk(design, rule.guard) : -1;
    bool on = evaluateRule(rule, outOn[i], v, mod, g);
    if (on != outOn[i]) setOutput(i, on);
  }
  for (int i = 0; i < board->relays; i++) if (manual[i] >= 0 && (bool)manual[i] != outOn[i]) setOutput(i, manual[i]);

  blocksShow(design, standIn ? "stand-in board" : board->name);
  if (board->led >= 0) digitalWrite(board->led, (now / 1000) % 2);

  logRecord();
  if (!ble::connected()) liveOn[LINK_BLE] = false;

  if (liveOn[LINK_SERIAL] || liveOn[LINK_BLE]) {
    JsonDocument doc;
    doc["type"] = "live";
    doc["time"] = nowUnix();
    fillValues(doc["values"].to<JsonObject>());
    JsonObject outs = doc["outputs"].to<JsonObject>();
    JsonObject man = doc["manual"].to<JsonObject>();
    for (int i = 0; i < board->relays; i++) {
      String p = String("OUT") + (i + 1);
      outs[p] = outOn[i] ? 1 : 0;
      man[p] = manual[i] >= 0;
    }
    if (liveOn[LINK_SERIAL]) sendTo(LINK_SERIAL, doc);
    if (liveOn[LINK_BLE]) sendTo(LINK_BLE, doc);
  }
}

// ---------- commands from the studio ----------

static bool changesThings(const char* cmd) {
  return !strcmp(cmd, "config") || !strcmp(cmd, "out") || !strcmp(cmd, "auto") || !strcmp(cmd, "log_clear");
}

static void handle(Link from, const String& text) {
  replyTo = from;
  JsonDocument in;
  if (deserializeJson(in, text)) { reply("error", false, "Not JSON."); return; }
  const char* cmd = in["cmd"] | "";
  if (from == LINK_BLE && changesThings(cmd) && (int32_t)(millis() - bleUnlockedUntilMs) > 0) {
    reply(cmd, false, "Press the PAIR button on the board, then try again within 2 minutes.");
    return;
  }

  if (!strcmp(cmd, "hello")) {
    JsonDocument doc;
    doc["type"] = "hello";
    doc["fw"] = FW_VERSION;
    doc["board"] = board->name;
    doc["standIn"] = standIn;
#ifdef ASC_STANDIN_GPSIOAM
    doc["standInKit"] = "gpsioam";
#endif
    doc["id"] = deviceId();
    doc["rtc"] = haveRtc;
    doc["time"] = nowUnix();
    doc["link"] = from == LINK_BLE ? "ble" : "usb";
    doc["unlocked"] = from == LINK_SERIAL || (int32_t)(millis() - bleUnlockedUntilMs) <= 0;
    doc["logBytes"] = fieldlog::bytesUsed();
    if (haveDesign) designToJson(design, doc["design"].to<JsonObject>());
    send(doc);
  } else if (!strcmp(cmd, "get_design")) {
    // The whole saved design, including the app layout the phone builds its screen from.
    JsonDocument doc;
    doc["type"] = "design";
    String saved = fieldlog::loadDesign();
    JsonDocument stored;
    if (saved.length() && !deserializeJson(stored, saved)) doc["design"] = stored;
    else doc["design"] = nullptr;
    send(doc);
  } else if (!strcmp(cmd, "config")) {
    String json;
    serializeJson(in["config"], json);
    String error;
    if (!applyDesign(json, error)) { reply("config", false, error); return; }
    if (!fieldlog::saveDesign(json)) { reply("config", false, "The board couldn't save the design. Try again."); return; }
    beep(60);
    JsonDocument doc;
    doc["type"] = "config";
    doc["ok"] = true;
    doc["design"] = design.version;
    send(doc);
  } else if (!strcmp(cmd, "time")) {
    clockUnix = in["unix"] | 0;
    clockAtMs = millis();
    if (haveRtc && clockUnix) rtc.adjust(DateTime(clockUnix));
    reply("time", clockUnix != 0);
  } else if (!strcmp(cmd, "live")) {
    liveOn[from] = in["on"] | true;
    reply("live", true);
  } else if (!strcmp(cmd, "out")) {
    int i = outIndex(in["port"] | "");
    if (i < 0) { reply("out", false, "No such output on this board."); return; }
    manual[i] = (in["on"] | false) ? 1 : 0;
    manualUntilMs[i] = millis() + 10UL * 60 * 1000;  // back to the rules after 10 minutes
    setOutput(i, manual[i]);
    reply("out", true);
  } else if (!strcmp(cmd, "auto")) {
    for (int i = 0; i < board->relays; i++) manual[i] = -1;
    reply("auto", true);
  } else if (!strcmp(cmd, "selftest")) {
    if (!haveDesign) { reply("selftest", false, "No design on the board yet. Send it from the Build stage."); return; }
    JsonDocument doc;
    doc["type"] = "selftest";
    blocksSelfTest(design, doc["results"].to<JsonArray>());
    send(doc);
  } else if (!strcmp(cmd, "log")) {
    // Stream the field log: one {"type":"logrec",...} per record, then {"type":"logend"}.
    uint32_t since = in["since"] | 0;
    size_t n = fieldlog::read(since, [&](const String& rec) {
      String line = String("{\"type\":\"logrec\",\"rec\":") + rec + "}";
      if (from == LINK_SERIAL) Serial.println(line); else ble::sendLine(line);
    });
    JsonDocument doc;
    doc["type"] = "logend";
    doc["ok"] = true;
    doc["count"] = n;
    send(doc);
  } else if (!strcmp(cmd, "log_clear")) {
    fieldlog::clear();
    reply("log_clear", true);
  } else if (!strcmp(cmd, "beep")) {
    beep(150);
    reply("beep", true);
  } else {
    reply("error", false, String("Unknown command '") + cmd + "'.");
  }
}

// ---------- Arduino ----------

void setup() {
  detectBoard();
  for (int i = 0; i < board->relays; i++) {
    if (board->out[i] >= 0) { pinMode(board->out[i], OUTPUT); digitalWrite(board->out[i], LOW); }
    manual[i] = -1;
    outOn[i] = false;
  }
  if (board->buzzer >= 0) { pinMode(board->buzzer, OUTPUT); digitalWrite(board->buzzer, LOW); }
  if (board->led >= 0) pinMode(board->led, OUTPUT);
  if (board->vbatEn >= 0) { pinMode(board->vbatEn, OUTPUT); digitalWrite(board->vbatEn, LOW); }

  Serial.begin(115200);
  line.reserve(4096);

  if (board->i2cIntSda >= 0) {
    Wire.begin(board->i2cIntSda, board->i2cIntScl);
    haveRtc = rtc.begin(&Wire);
  }
  if (haveRtc && !rtc.lostPower()) { clockUnix = rtc.now().unixtime(); clockAtMs = millis(); }

  fieldlog::begin();
  // Firmware 0.1 kept the design in NVS; move it to flash once.
  prefs.begin("asc", false);
  if (prefs.isKey("design")) {
    String old = prefs.getString("design", "");
    if (old.length()) fieldlog::saveDesign(old);
    prefs.remove("design");
  }
  String saved = fieldlog::loadDesign();
  if (saved.length()) {
    String error;
    if (!applyDesign(saved, error)) fieldlog::removeDesign();  // a design for another board, or from an older firmware
  }

  if (board->pairBtn >= 0) pinMode(board->pairBtn, INPUT_PULLUP);
  ble::begin(deviceId());
}

static void checkPairButton() {
  static bool wasDown = false;
  bool down = board->pairBtn >= 0 && digitalRead(board->pairBtn) == LOW;
  if (down && !wasDown) {
    bleUnlockedUntilMs = millis() + BLE_UNLOCK_MS;
    beep(40);
  }
  wasDown = down;
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      line.trim();
      if (line.length()) handle(LINK_SERIAL, line);
      line = "";
    } else if (line.length() < 8192) {
      line += c;
    }
  }
  String bleLine;
  if (ble::takeLine(bleLine)) {
    bleLine.trim();
    if (bleLine.length()) handle(LINK_BLE, bleLine);
  }
  checkPairButton();
  if (millis() - lastTickMs >= 1000) {
    lastTickMs = millis();
    tick();
  }
}
