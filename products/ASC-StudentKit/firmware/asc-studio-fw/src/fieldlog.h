// The field log: one line of readings every few minutes, kept in flash
// (LittleFS) so a field trial works with no WiFi. The studio downloads it
// over USB (or the app over Bluetooth) for the stage-9 report.
#pragma once
#include <Arduino.h>
#include <functional>

namespace fieldlog {

bool begin();                                 // mounts LittleFS (formats it on first use)
void append(const String& jsonLine);          // one record; rotates when the file gets large
// Calls `each` for every record with time >= since (oldest first); returns how many.
size_t read(uint32_t since, const std::function<void(const String&)>& each);
void clear();
size_t bytesUsed();

// The saved design lives here too: NVS strings are limited to about 4 KB,
// and a design with its app layout can be larger.
bool saveDesign(const String& json);
String loadDesign();
void removeDesign();

}  // namespace fieldlog
