#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

// Rolling log of completed runs — both scheduled (auto) and manual
// valve/dosing toggles — one JSON line per record in /history.jsonl.
// Capped by total file size, not record count, so it degrades
// predictably regardless of how verbose a record gets; the oldest
// quarter of records is dropped in one pass whenever the cap is hit,
// rather than trimming on every single append past the cap.
//
// LittleFS.begin(true) auto-formats an unformatted/missing filesystem
// partition on first boot. If the board's partition table genuinely has
// no spiffs/littlefs region reserved, begin() returns false and history
// recording is silently disabled (logged once) — this was written
// without a live device to verify the actual partition layout against,
// so treat that failure path as expected until confirmed otherwise.
class RunHistory {
public:
  void begin() {
    _ready = LittleFS.begin(true);
    if (!_ready) {
      Serial.println("[History] LittleFS mount failed — run history disabled");
    }
  }

  bool ready() const { return _ready; }

  // source: "auto" or "manual". name is either "Program - Sequence"
  // (auto) or a stable channel key like "valve3"/"dosing" (manual) —
  // the app resolves the manual case to whatever that channel's
  // CURRENT display name is, rather than baking in a name that could
  // go stale if the user renames it later.
  void record(time_t startEpoch, uint32_t durationSec, const String& name,
              const String& source, float volumeLiters) {
    if (!_ready || durationSec == 0) return;

    JsonDocument doc;
    doc["ts"] = (uint32_t)startEpoch;
    doc["dur"] = durationSec;
    doc["name"] = name;
    doc["src"] = source;
    doc["vol"] = volumeLiters;
    String line;
    serializeJson(doc, line);

    File f = LittleFS.open(HISTORY_PATH, "a");
    if (!f) {
      Serial.println("[History] open-for-append failed");
      return;
    }
    f.println(line);
    f.close();

    _maybeTrim();
  }

  // Up to maxRecords records with ts >= sinceEpoch, newest first, as a
  // JSON array string. The file is capped (see _maybeTrim) so reading
  // it fully into RAM to reverse it stays bounded.
  String queryJson(time_t sinceEpoch, uint16_t maxRecords) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (!_ready) {
      String out;
      serializeJson(doc, out);
      return out;
    }

    std::vector<String> matched;
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (f) {
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;
        JsonDocument rec;
        if (deserializeJson(rec, line)) continue;
        uint32_t ts = rec["ts"] | 0;
        if ((time_t)ts >= sinceEpoch) matched.push_back(line);
      }
      f.close();
    }

    uint16_t count = 0;
    for (int i = (int)matched.size() - 1; i >= 0 && count < maxRecords; i--, count++) {
      JsonDocument rec;
      deserializeJson(rec, matched[(size_t)i]);
      arr.add(rec.as<JsonObject>());
    }
    String out;
    serializeJson(doc, out);
    return out;
  }

private:
  static constexpr const char* HISTORY_PATH = "/history.jsonl";
  // ~64KB comfortably covers 3 months of a small farm's realistic run
  // frequency (a handful of records/day) at this record size, while
  // keeping the full-file read in _maybeTrim/queryJson small enough to
  // not be a real concern against the board's ~320KB RAM.
  static constexpr size_t MAX_BYTES = 64 * 1024;

  void _maybeTrim() {
    File f = LittleFS.open(HISTORY_PATH, "r");
    if (!f) return;
    size_t sz = f.size();
    if (sz <= MAX_BYTES) {
      f.close();
      return;
    }

    std::vector<String> lines;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (!line.isEmpty()) lines.push_back(line);
    }
    f.close();

    size_t drop = lines.size() / 4;
    File out = LittleFS.open(HISTORY_PATH, "w");
    if (!out) return;
    for (size_t i = drop; i < lines.size(); i++) out.println(lines[i]);
    out.close();
    Serial.printf("[History] Trimmed %u oldest record(s), file was %u bytes\n", (unsigned)drop, (unsigned)sz);
  }

  bool _ready = false;
};
