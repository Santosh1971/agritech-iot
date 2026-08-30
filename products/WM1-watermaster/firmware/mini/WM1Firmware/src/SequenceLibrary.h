#pragma once
#include <Preferences.h>
#include "Config.h"
#include "Scheduler.h"  // for the Sequence struct

// A separate, reusable pool of named Sequence templates — "Drip Zone A,
// 20 min", "Sprinkler B+C, mid-dose" — that a Program is assembled FROM,
// so the farmer doesn't redefine the same valve/duration/dosing
// combination from scratch in every program that needs it.
//
// Deliberately COPY semantics, not live references: when a program is
// built by picking library entries, each picked entry's data is copied
// into that program's own embedded Sequence (see Scheduler.h's Program
// struct, unchanged). Editing or deleting a library entry later never
// retroactively changes a program that already copied it. This keeps
// the proven Scheduler engine completely untouched — it still only
// ever sees concrete, self-contained Program/Sequence data, exactly as
// before this existed.
class SequenceLibrary {
public:
  static constexpr uint8_t MAX_ENTRIES = 20;

  struct Entry {
    uint8_t id = 0;
    bool inUse = false;
    Sequence seq;
  };

  void begin() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    _count = prefs.getUChar("seqlib_count", 0);
    for (uint8_t i = 0; i < _count && i < MAX_ENTRIES; i++) {
      char key[12];
      snprintf(key, sizeof(key), "seqlib%u", i);
      size_t len = prefs.getBytesLength(key);
      if (len == sizeof(Entry)) {
        prefs.getBytes(key, &_entries[i], sizeof(Entry));
      }
    }
    prefs.end();
  }

  void saveAll() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("seqlib_count", _count);
    for (uint8_t i = 0; i < _count; i++) {
      char key[12];
      snprintf(key, sizeof(key), "seqlib%u", i);
      prefs.putBytes(key, &_entries[i], sizeof(Entry));
    }
    prefs.end();
  }

  uint8_t count() const { return _count; }
  const Entry* entryAt(uint8_t i) const { return (i < _count) ? &_entries[i] : nullptr; }

  // Full replace, same pattern as CommandHandler's set_programs — the
  // app always sends the complete desired library, simplest correct
  // model given how infrequently this changes relative to status/manual
  // traffic.
  void replaceAll(const Entry* newEntries, uint8_t newCount) {
    _count = (newCount > MAX_ENTRIES) ? MAX_ENTRIES : newCount;
    for (uint8_t i = 0; i < _count; i++) {
      _entries[i] = newEntries[i];
      _entries[i].id = i;
      _entries[i].inUse = true;
    }
    saveAll();
  }

private:
  Entry _entries[MAX_ENTRIES];
  uint8_t _count = 0;
};
