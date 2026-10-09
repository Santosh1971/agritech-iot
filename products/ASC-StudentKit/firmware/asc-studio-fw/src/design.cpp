#include "design.h"

namespace {

struct BlockDef {
  const char* id;
  PortKind kind;
};

// Must match lib/studio/blocks.ts. Blocks for Mega-only ports (npk, lora,
// gsm) are accepted by the studio's rules only on Mega and are not driven by
// this version of the firmware yet.
const BlockDef BLOCKS[] = {
  {"soil", KIND_S}, {"soilt", KIND_S}, {"float", KIND_S}, {"flow", KIND_S}, {"dht", KIND_S}, {"rain", KIND_S},
  {"probet", KIND_S}, {"raing", KIND_S}, {"pir", KIND_S}, {"door", KIND_S}, {"tds", KIND_S}, {"level", KIND_S},
  {"bme", KIND_I2C}, {"light", KIND_I2C}, {"sht", KIND_I2C}, {"co2", KIND_I2C}, {"irtemp", KIND_I2C},
  {"pump", KIND_OUT}, {"fogger", KIND_OUT}, {"valve", KIND_OUT}, {"valve24", KIND_OUT}, {"siren", KIND_OUT},
  {"doser", KIND_OUT}, {"fan", KIND_OUT}, {"growlite", KIND_OUT}, {"heater", KIND_OUT},
};

const BlockDef* blockDef(const char* id) {
  for (const auto& b : BLOCKS) if (strcmp(b.id, id) == 0) return &b;
  return nullptr;
}

// Port name -> kind and pin on this board. Returns false if the board has no such port.
bool portInfo(const BoardMap& board, const char* port, PortKind& kind, int8_t& gpio) {
  int n = 0;
  if (sscanf(port, "S%d", &n) == 1 && n >= 1 && n <= board.sensorPorts && strlen(port) <= 2) {
    kind = KIND_S; gpio = board.sensor[n - 1]; return gpio >= 0;
  }
  if (sscanf(port, "OUT%d", &n) == 1 && n >= 1 && n <= board.relays) {
    kind = KIND_OUT; gpio = board.out[n - 1]; return gpio >= 0;
  }
  if (sscanf(port, "I2C-%d", &n) == 1 && n >= 1 && n <= board.i2cPorts) {
    kind = KIND_I2C; gpio = -1; return true;
  }
  return false;
}

int16_t parseHhMm(const char* s) {
  int h, m;
  if (!s || sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

void copy(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

}  // namespace

Slot* findSlot(Design& d, const char* port) {
  for (int i = 0; i < d.nSlots; i++) if (strcmp(d.slots[i].port, port) == 0) return &d.slots[i];
  return nullptr;
}

bool parseDesign(JsonVariantConst json, const BoardMap& board, Design& out, String& error) {
  Design d;
  memset(&d, 0, sizeof(d));
  d.version = json["design"] | 0;
  copy(d.name, sizeof(d.name), json["name"] | "ASC kit");
  d.tzOffsetMin = json["tzOffsetMin"] | 330;  // IST

  const char* kit = json["kit"] | "";
  if (strcmp(kit, board.name) != 0) {
    error = String("This design is for the ") + kit + " kit, but this board is a " + board.name + ".";
    return false;
  }

  JsonObjectConst ports = json["ports"];
  int levels = 0;
  for (JsonPairConst kv : ports) {
    const char* port = kv.key().c_str();
    const char* block = kv.value() | "";
    if (!block[0]) continue;
    PortKind kind; int8_t gpio;
    if (!portInfo(board, port, kind, gpio)) { error = String("This board has no port ") + port + "."; return false; }
    const BlockDef* b = blockDef(block);
    if (!b) { error = String("This firmware can't drive '") + block + "' yet."; return false; }
    if (b->kind != kind) { error = String("'") + block + "' can't go on " + port + "."; return false; }
    if (strcmp(block, "level") == 0 && ++levels > 1) {
      error = "Only one tank level sensor fits: it uses the board's one spare serial port.";
      return false;
    }
    if (d.nSlots >= MAX_SLOTS) { error = "Too many blocks."; return false; }
    Slot& s = d.slots[d.nSlots++];
    copy(s.port, sizeof(s.port), port);
    copy(s.block, sizeof(s.block), block);
    s.kind = kind;
    s.gpio = gpio;
    s.dryMv = json["cal"][port]["dry"] | 2600.0f;
    s.wetMv = json["cal"][port]["wet"] | 1100.0f;
    for (int i = 0; i < MAX_VALUES; i++) { s.values[i] = NAN; s.valueKeys[i] = ""; }
  }

  for (JsonObjectConst r : json["rules"].as<JsonArrayConst>()) {
    if (d.nRules >= MAX_RULES) { error = "Too many rules."; return false; }
    Rule& rule = d.rules[d.nRules];
    copy(rule.out, sizeof(rule.out), r["out"] | "");
    copy(rule.sensor, sizeof(rule.sensor), r["sensor"] | "");
    copy(rule.guard, sizeof(rule.guard), r["guard"] | "");
    rule.below = strcmp(r["when"] | "below", "below") == 0;
    rule.on = r["on"] | NAN;
    rule.off = r["off"] | NAN;
    rule.fromMin = parseHhMm(r["from"] | (const char*)nullptr);
    rule.toMin = parseHhMm(r["to"] | (const char*)nullptr);
    Slot* outSlot = findSlot(d, rule.out);
    if (!outSlot || outSlot->kind != KIND_OUT) { error = String("A rule switches ") + rule.out + ", which has no output on it."; return false; }
    char port[8];
    copy(port, sizeof(port), rule.sensor);
    if (char* colon = strchr(port, ':')) *colon = 0;
    Slot* in = findSlot(d, port);
    if (!in || in->kind == KIND_OUT) { error = String("A rule reads ") + rule.sensor + ", which has no sensor on it."; return false; }
    if (rule.guard[0] && !findSlot(d, rule.guard)) { error = String("A rule waits for ") + rule.guard + ", which has no sensor on it."; return false; }
    if (isnan(rule.on) || isnan(rule.off)) { error = "A rule is missing its ON or OFF level."; return false; }
    d.nRules++;
  }

  out = d;
  return true;
}

void designToJson(const Design& d, JsonObject out) {
  out["design"] = d.version;
  out["name"] = d.name;
  JsonObject ports = out["ports"].to<JsonObject>();
  for (int i = 0; i < d.nSlots; i++) ports[d.slots[i].port] = d.slots[i].block;
  out["rules"] = d.nRules;
}
