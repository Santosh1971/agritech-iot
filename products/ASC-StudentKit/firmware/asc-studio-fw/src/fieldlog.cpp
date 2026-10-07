#include "fieldlog.h"

#include <LittleFS.h>

namespace fieldlog {
namespace {

const char* LOG = "/log.jsonl";
const char* OLD = "/log.old";
const char* DESIGN = "/design.json";
const size_t ROTATE_AT = 256 * 1024;  // about 14 days at one record every 10 minutes
bool mounted = false;

size_t readFile(const char* path, uint32_t since, const std::function<void(const String&)>& each) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  size_t n = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!line.length()) continue;
    // Records start {"t":<unix>,... so the time can be read without parsing the JSON.
    uint32_t t = (uint32_t)strtoul(line.c_str() + 5, nullptr, 10);
    if (t >= since) { each(line); n++; }
  }
  f.close();
  return n;
}

}  // namespace

bool begin() {
  mounted = LittleFS.begin(true);
  return mounted;
}

void append(const String& jsonLine) {
  if (!mounted) return;
  File cur = LittleFS.open(LOG, "r");
  size_t size = cur ? cur.size() : 0;
  if (cur) cur.close();
  if (size > ROTATE_AT) {
    LittleFS.remove(OLD);
    LittleFS.rename(LOG, OLD);
  }
  File f = LittleFS.open(LOG, "a");
  if (!f) return;
  f.print(jsonLine);
  f.print('\n');
  f.close();
}

size_t read(uint32_t since, const std::function<void(const String&)>& each) {
  if (!mounted) return 0;
  return readFile(OLD, since, each) + readFile(LOG, since, each);
}

void clear() {
  if (!mounted) return;
  LittleFS.remove(OLD);
  LittleFS.remove(LOG);
}

size_t bytesUsed() {
  return mounted ? LittleFS.usedBytes() : 0;
}

bool saveDesign(const String& json) {
  if (!mounted) return false;
  File f = LittleFS.open(DESIGN, "w");
  if (!f) return false;
  size_t n = f.print(json);
  f.close();
  return n == json.length();
}

String loadDesign() {
  if (!mounted) return "";
  File f = LittleFS.open(DESIGN, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

void removeDesign() {
  if (mounted) LittleFS.remove(DESIGN);
}

}  // namespace fieldlog
