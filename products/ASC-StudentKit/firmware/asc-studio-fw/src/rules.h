// The rule engine: decides whether one output should be ON.
// Header-only and free of Arduino calls so `pio test -e native` can test it
// on a PC. The studio writes the rules (lib/studio/automation.ts); this is
// the only place they are interpreted.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

struct Rule {
  char out[8];       // "OUT1"
  char sensor[12];   // "S1" or "S3:t" for a block with several values
  bool below;        // true: ON below `on`, OFF at `off` (off > on). false: ON above `on`, OFF at `off` (off < on)
  float on;
  float off;
  int16_t fromMin;   // time window, minutes after midnight; -1 = all day
  int16_t toMin;
  char guard[8];     // a sensor port that must read "safe to run" (float: water present, rain: dry); "" = none
};

static inline bool inWindow(int16_t from, int16_t to, int minuteOfDay) {
  if (from < 0 || to < 0) return true;
  if (minuteOfDay < 0) return false;  // time not known yet: fail safe
  if (from <= to) return minuteOfDay >= from && minuteOfDay < to;
  return minuteOfDay >= from || minuteOfDay < to;  // window crosses midnight
}

// value: NAN when the sensor failed or is missing. guardOk: 1 safe, 0 not safe, -1 no guard reading.
static inline bool evaluateRule(const Rule& r, bool currentlyOn, float value, int minuteOfDay, int guardOk) {
  if (isnan(value)) return false;
  if (r.guard[0] && guardOk != 1) return false;
  if (!inWindow(r.fromMin, r.toMin, minuteOfDay)) return false;
  if (r.below) return currentlyOn ? value < r.off : value < r.on;
  return currentlyOn ? value > r.off : value > r.on;
}
