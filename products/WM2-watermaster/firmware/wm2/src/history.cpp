#include "history.h"

#include <LittleFS.h>

#include "timeutil.h"

namespace history {

static bool ok = false;

bool begin() {
  ok = LittleFS.begin(true);
  if (ok && !LittleFS.exists("/r")) LittleFS.mkdir("/r");
  return ok;
}

size_t usedBytes() { return ok ? LittleFS.usedBytes() : 0; }
size_t totalBytes() { return ok ? LittleFS.totalBytes() : 0; }

static String rawPath(uint32_t day) { return "/r/" + String(day) + ".bin"; }

void add(const Rec& r, uint32_t localDay) {
  if (!ok) return;
  File f = LittleFS.open(rawPath(localDay), "a");
  if (!f) return;
  f.write((const uint8_t*)&r, sizeof r);
  f.close();
}

static void appendLine(const char* path, const char* rotateTo, size_t maxBytes, const String& line) {
  if (!ok) return;
  File f = LittleFS.open(path, "a");
  if (!f) return;
  f.println(line);
  size_t sz = f.size();
  f.close();
  if (rotateTo && sz > maxBytes) {
    LittleFS.remove(rotateTo);
    LittleFS.rename(path, rotateTo);
  }
}

void addEvent(const String& l) { appendLine("/ev.log", "/ev1.log", 65536, l); }
void addDay(const String& l) { appendLine("/days.log", "/days1.log", 262144, l); }

// Sorted list of raw day files present.
static int listDays(uint32_t* out, int max) {
  int n = 0;
  File dir = LittleFS.open("/r");
  if (!dir) return 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    out[n++] = (uint32_t)name.toInt();
    f.close();
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && out[j - 1] > out[j]; j--) { uint32_t t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
  return n;
}

void housekeeping() {
  if (!ok) return;
  while (LittleFS.usedBytes() > LittleFS.totalBytes() * 85 / 100) {
    uint32_t d[400];
    int n = listDays(d, 400);
    if (n <= 1) break;  // keep today
    LittleFS.remove(rawPath(d[0]));
  }
}

// Visit raw records in [from, to].
template <typename F>
static void scan(uint32_t fromUtc, uint32_t toUtc, int16_t tzMin, F fn) {
  if (!ok) return;
  int32_t tz = tzMin * 60;
  uint32_t d0 = (uint32_t)(((int64_t)fromUtc + tz) / 86400);
  uint32_t d1 = (uint32_t)(((int64_t)toUtc + tz) / 86400);
  // Records are filed under the day boundary in use, so look one day either side.
  for (uint32_t d = d0 > 0 ? d0 - 1 : 0; d <= d1 + 1; d++) {
    File f = LittleFS.open(rawPath(d), "r");
    if (!f) continue;
    Rec r;
    while (f.read((uint8_t*)&r, sizeof r) == sizeof r) {
      if (r.ts < fromUtc || r.ts > toUtc) continue;
      if (!fn(r)) { f.close(); return; }
    }
    f.close();
  }
}

void query(uint32_t fromUtc, uint32_t toUtc, const char* res, int16_t tzMin, JsonObject out) {
  uint32_t span = toUtc > fromUtc ? toUtc - fromUtc : 0;
  String r = res ? res : "";
  if (r.length() == 0) r = span <= 2 * 86400u ? "raw" : span <= 31 * 86400u ? "hour" : "day";
  uint32_t bucket = r == "hour" ? 3600 : r == "day" ? 86400 : 0;
  out["from"] = fromUtc;
  out["to"] = toUtc;
  out["res"] = r;

  float totL[4] = {0, 0, 0, 0};
  uint32_t openSec[2] = {0, 0}, waterSec = 0, recs = 0;
  uint16_t minBatt = 0xFFFF;
  JsonArray pts = out["points"].to<JsonArray>();
  // Current bucket accumulators
  int64_t curKey = -1;
  float bL[4] = {0, 0, 0, 0};
  uint32_t bOpen[2] = {0, 0}, bWater = 0;
  uint16_t bMinBatt = 0xFFFF;
  int pointCount = 0;
  const int MAX_POINTS = 1500;
  int32_t tz = tzMin * 60;

  auto flush = [&]() {
    if (curKey < 0 || pointCount >= MAX_POINTS) return;
    JsonArray p = pts.add<JsonArray>();
    p.add((uint32_t)(curKey * bucket - tz));  // bucket start, UTC
    for (int i = 0; i < 4; i++) p.add(roundf(bL[i] * 10) / 10);
    p.add(bOpen[0] / 60);
    p.add(bOpen[1] / 60);
    p.add(bWater / 60);
    p.add(bMinBatt == 0xFFFF ? 0 : bMinBatt / 100.0f);
    pointCount++;
  };

  scan(fromUtc, toUtc, tzMin, [&](const Rec& rec) {
    recs++;
    for (int i = 0; i < 4; i++) totL[i] += rec.l10[i] / 10.0f;
    if (rec.flags & 0x04) openSec[0] += rec.dt;
    if (rec.flags & 0x08) openSec[1] += rec.dt;
    if (rec.flags & 0x02) waterSec += rec.dt;
    if (rec.battCv && rec.battCv < minBatt) minBatt = rec.battCv;
    if (bucket == 0) {
      if (pointCount < MAX_POINTS) {
        JsonArray p = pts.add<JsonArray>();
        p.add(rec.ts);
        for (int i = 0; i < 4; i++) p.add(rec.lpm10[i] / 10.0f);
        p.add(rec.flags);
        p.add(rec.battCv / 100.0f);
        pointCount++;
      }
      return true;
    }
    int64_t key = ((int64_t)rec.ts + tz) / bucket;
    if (key != curKey) {
      flush();
      curKey = key;
      for (int i = 0; i < 4; i++) bL[i] = 0;
      bOpen[0] = bOpen[1] = bWater = 0;
      bMinBatt = 0xFFFF;
    }
    for (int i = 0; i < 4; i++) bL[i] += rec.l10[i] / 10.0f;
    if (rec.flags & 0x04) bOpen[0] += rec.dt;
    if (rec.flags & 0x08) bOpen[1] += rec.dt;
    if (rec.flags & 0x02) bWater += rec.dt;
    if (rec.battCv && rec.battCv < bMinBatt) bMinBatt = rec.battCv;
    return true;
  });
  if (bucket) flush();

  out["columns"] = bucket == 0 ? "ts,lpm1,lpm2,lpm3,lpm4,flags,batt" : "ts,l1,l2,l3,l4,v1_min,v2_min,water_min,min_batt";
  JsonObject t = out["totals"].to<JsonObject>();
  JsonArray tl = t["litres"].to<JsonArray>();
  for (int i = 0; i < 4; i++) tl.add(roundf(totL[i] * 10) / 10);
  JsonArray vl = t["valve_litres"].to<JsonArray>();
  vl.add(roundf((totL[0] + totL[1]) * 10) / 10);
  vl.add(roundf((totL[2] + totL[3]) * 10) / 10);
  JsonArray om = t["valve_open_min"].to<JsonArray>();
  om.add(openSec[0] / 60);
  om.add(openSec[1] / 60);
  t["water_min"] = waterSec / 60;
  t["min_batt"] = minBatt == 0xFFFF ? 0 : minBatt / 100.0f;
  t["records"] = recs;
  out["truncated"] = pointCount >= MAX_POINTS;
}

void days(uint32_t fromDay, uint32_t toDay, JsonArray out) {
  const char* files[] = {"/days1.log", "/days.log"};
  for (const char* p : files) {
    File f = LittleFS.open(p, "r");
    if (!f) continue;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      JsonDocument d;
      if (deserializeJson(d, l)) continue;
      uint32_t day = d["day"] | 0;
      if (day >= fromDay && day <= toDay && out.size() < 400) out.add(d);
    }
    f.close();
  }
}

void events(uint16_t limit, JsonArray out) {
  // Keep the newest `limit` lines across both files.
  static const int CAP = 200;
  if (limit > CAP) limit = CAP;
  String* ring = new String[limit];
  int n = 0;
  const char* files[] = {"/ev1.log", "/ev.log"};
  for (const char* p : files) {
    File f = LittleFS.open(p, "r");
    if (!f) continue;
    while (f.available()) {
      ring[n % limit] = f.readStringUntil('\n');
      n++;
    }
    f.close();
  }
  int start = n > limit ? n - limit : 0;
  for (int i = start; i < n; i++) {
    JsonDocument d;
    if (!deserializeJson(d, ring[i % limit])) out.add(d);
  }
  delete[] ring;
}

void csvBegin(CsvCursor& c, uint32_t fromUtc, uint32_t toUtc, int16_t tzMin) {
  c.from = fromUtc;
  c.to = toUtc;
  c.tz = tzMin * 60;
  uint32_t d0 = (uint32_t)(((int64_t)fromUtc + c.tz) / 86400);
  c.day = d0 > 0 ? d0 - 1 : 0;
  c.lastDay = (uint32_t)(((int64_t)toUtc + c.tz) / 86400) + 1;
  c.pos = 0;
  c.header = false;
  c.done = false;
  c.pending = "";
}

static bool csvNextLine(CsvCursor& c) {
  if (!c.header) {
    c.header = true;
    c.pending = "utc,local_time,dt_s,pressure,water,v1_open,v2_open,bypass,batt_v,rssi,lpm1,lpm2,lpm3,lpm4,l1,l2,l3,l4\n";
    return true;
  }
  while (c.day <= c.lastDay) {
    File f = LittleFS.open(rawPath(c.day), "r");
    if (f && f.seek(c.pos)) {
      Rec r;
      while (f.read((uint8_t*)&r, sizeof r) == sizeof r) {
        c.pos += sizeof r;
        if (r.ts < c.from || r.ts > c.to) continue;
        f.close();
        char b[220];
        snprintf(b, sizeof b, "%lu,%s,%u,%d,%d,%d,%d,%d,%.2f,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                 (unsigned long)r.ts, fmtLocal(r.ts + c.tz).c_str(), r.dt, r.flags & 1, (r.flags >> 1) & 1,
                 (r.flags >> 2) & 1, (r.flags >> 3) & 1, (r.flags >> 4) & 1, r.battCv / 100.0f, r.rssi,
                 r.lpm10[0] / 10.0f, r.lpm10[1] / 10.0f, r.lpm10[2] / 10.0f, r.lpm10[3] / 10.0f, r.l10[0] / 10.0f,
                 r.l10[1] / 10.0f, r.l10[2] / 10.0f, r.l10[3] / 10.0f);
        c.pending = b;
        return true;
      }
    }
    if (f) f.close();
    c.day++;
    c.pos = 0;
  }
  c.done = true;
  return false;
}

size_t csvChunk(CsvCursor& c, uint8_t* buf, size_t max) {
  size_t n = 0;
  while (n < max) {
    if (c.pending.length() == 0 && (c.done || !csvNextLine(c))) break;
    size_t take = c.pending.length();
    if (take > max - n) take = max - n;
    memcpy(buf + n, c.pending.c_str(), take);
    n += take;
    c.pending = c.pending.substring(take);
  }
  return n;
}

int since(uint32_t afterUtc, Rec* buf, int max) {
  if (!ok) return 0;
  uint32_t d[400];
  int nd = listDays(d, 400);
  int n = 0;
  for (int i = 0; i < nd && n < max; i++) {
    File f = LittleFS.open(rawPath(d[i]), "r");
    if (!f) continue;
    Rec r;
    while (n < max && f.read((uint8_t*)&r, sizeof r) == sizeof r)
      if (r.ts > afterUtc) buf[n++] = r;
    f.close();
  }
  return n;
}

void wipe() {
  if (!ok) return;
  uint32_t d[400];
  int n = listDays(d, 400);
  for (int i = 0; i < n; i++) LittleFS.remove(rawPath(d[i]));
  LittleFS.remove("/ev.log");
  LittleFS.remove("/ev1.log");
  LittleFS.remove("/days.log");
  LittleFS.remove("/days1.log");
}

}  // namespace history
