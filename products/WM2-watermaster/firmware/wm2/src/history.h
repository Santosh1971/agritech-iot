#pragma once
// On-device history in LittleFS.
//   /r/<localDay>.bin  raw records (32 bytes each), oldest files deleted when space runs low
//   /days.log          one JSON line per valve per programmed day (day_result)
//   /ev.log, /ev1.log  JSON event lines, rotated at 64 KB
#include <Arduino.h>
#include <ArduinoJson.h>

#pragma pack(push, 1)
struct Rec {
  uint32_t ts;        // UTC
  uint16_t dt;        // seconds covered by this record
  uint8_t flags;      // bit0 ps, 1 water, 2 v1 open, 3 v2 open, 4 bypass, 5 active
  int8_t rssi;
  uint16_t battCv;    // centivolts
  uint16_t lpm10[4];  // L/min x10 at record time
  uint16_t l10[4];    // litres x10 during dt
  uint8_t reserved[6];
};
#pragma pack(pop)
static_assert(sizeof(Rec) == 32, "Rec must be 32 bytes");

namespace history {

bool begin();
void add(const Rec& r, uint32_t localDay);
void addEvent(const String& jsonLine);
void addDay(const String& jsonLine);
void housekeeping();                 // free space if needed
size_t usedBytes();
size_t totalBytes();

// Summary + series for [fromUtc, toUtc]. res: "raw", "hour", "day" or "" (auto).
void query(uint32_t fromUtc, uint32_t toUtc, const char* res, int16_t tzMin, JsonObject out);
// Day results between two local days (inclusive).
void days(uint32_t fromDay, uint32_t toDay, JsonArray out);
// Last `limit` events (newest last).
void events(uint16_t limit, JsonArray out);
// CSV export, produced in chunks for a streamed HTTP response.
struct CsvCursor {
  uint32_t from = 0, to = 0;
  int32_t tz = 0;
  uint32_t day = 0, lastDay = 0, pos = 0;
  bool header = false, done = false;
  String pending;
};
void csvBegin(CsvCursor& c, uint32_t fromUtc, uint32_t toUtc, int16_t tzMin);
size_t csvChunk(CsvCursor& c, uint8_t* buf, size_t max);  // 0 = finished
// Records newer than `afterUtc`, up to max, for store-and-forward. Returns count.
int since(uint32_t afterUtc, Rec* buf, int max);
void wipe();

}  // namespace history
