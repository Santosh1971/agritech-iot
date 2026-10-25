#pragma once
// Shared state of the running node, used by commands, network and web code.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "irrigation_core.h"

#define FW_VERSION "0.1.0"

extern wm2core::Core core;

// One lock around the logic, settings and file system: the web server runs
// in its own task, the main loop in another.
extern SemaphoreHandle_t gLock;
struct Lock {
  Lock() { xSemaphoreTakeRecursive(gLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(gLock); }
};

// Time: UTC from RTC / NTP / phone, local = UTC + cfg.tzMin
uint32_t nowUtc();
bool timeValid();
void setTimeUtc(uint32_t utc, const char* source);
uint32_t localEpoch();
uint32_t todayIndex();  // local program day (with day_start_hour)

// Bypass of the water interlock, auto-expiring.
bool bypassOn();
uint32_t bypassLeftS();
void setBypass(bool on, uint32_t minutes, const char* src);

// Remember who sent the last command for a valve (for its events).
void noteSource(int valve, const char* src);

void buildState(JsonObject o);
void emitEvent(JsonDocument& d);   // adds v/ts/node, logs it, sends on MQTT + WS
void stateChanged();               // push state to MQTT/WS soon
void applyCoreConfig();            // after settings changed
void saveProgressNow();
void requestReboot(uint32_t delayMs);
void flowStatus(JsonArray out);
