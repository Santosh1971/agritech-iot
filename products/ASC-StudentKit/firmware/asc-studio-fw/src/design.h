// A student's design as the firmware holds it: which block is on which
// port, the rules, and the sensor calibration. Arrives as JSON from the
// studio (see lib/studio/deviceConfig.ts) and is kept in NVS.
#pragma once
#include <ArduinoJson.h>
#include "board_map.h"
#include "rules.h"

static const int MAX_SLOTS = 14;  // 8 sensor + 2 I2C + 4 outputs
static const int MAX_RULES = 4;
static const int MAX_VALUES = 3;

enum PortKind : uint8_t { KIND_S, KIND_I2C, KIND_OUT, KIND_OTHER };

struct Slot {
  char port[8];      // "S1", "I2C-1", "OUT1"
  char block[10];    // block id from the studio's library: "soil", "pump"...
  PortKind kind;
  int8_t gpio;       // sensor or relay pin; -1 for I2C
  // Calibration for analog soil probes (millivolts when dry / in water).
  float dryMv, wetMv;
  // Latest readings. NAN = not read or failed.
  float values[MAX_VALUES];
  const char* valueKeys[MAX_VALUES];  // "" for a single value, else "t", "h", "p"
  uint8_t nValues;
  bool ok;           // last read succeeded
};

struct Design {
  uint32_t version;  // StudioDesign.version, so the studio can tell what's on the board
  char name[40];
  int16_t tzOffsetMin;
  Slot slots[MAX_SLOTS];
  uint8_t nSlots;
  Rule rules[MAX_RULES];
  uint8_t nRules;
};

// Parse and check a design against this board. On failure `error` says why
// in words the studio can show a student, and `out` is left untouched.
bool parseDesign(JsonVariantConst json, const BoardMap& board, Design& out, String& error);
void designToJson(const Design& d, JsonObject out);
Slot* findSlot(Design& d, const char* port);
