#pragma once
#include <Preferences.h>
#include "Scheduler.h"
#include "Config.h"

// Simplest possible persistence: each Program struct is fixed-size and
// POD (no pointers inside it), so it can be stored/loaded as a raw
// byte blob per slot. Good enough for v0.1 bench testing — if this
// struct's layout changes later (e.g. growing MAX_PROGRAMS or field
// types), old saved blobs won't be compatible and will need a
// migration step, same class of issue FG1 hit with its Cycle JSON
// format change (see project notes: eh/em -> durationMinutes migration).

class ProgramStore {
public:
  static constexpr uint8_t MAX_SLOTS = 10;

  void begin() { _prefs.begin(NVS_NAMESPACE, false); }

  uint8_t loadAll(Program* outSlots[]) {
    uint8_t count = _prefs.getUChar("progCount", 0);
    for (uint8_t i = 0; i < count && i < MAX_SLOTS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "prog%u", i);
      size_t len = _prefs.getBytesLength(key);
      if (len == sizeof(Program)) {
        _prefs.getBytes(key, outSlots[i], sizeof(Program));
      }
    }
    return count;
  }

  void saveAll(Program* slots[], uint8_t count) {
    _prefs.putUChar("progCount", count);
    for (uint8_t i = 0; i < count && i < MAX_SLOTS; i++) {
      char key[8];
      snprintf(key, sizeof(key), "prog%u", i);
      _prefs.putBytes(key, slots[i], sizeof(Program));
    }
  }

private:
  Preferences _prefs;
};
