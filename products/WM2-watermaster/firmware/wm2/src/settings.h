#pragma once

// The two broker profiles are fixed: the user only picks AgriSense or Girish
// (and zone/node for Girish). Logins are compiled in at build time by
// wm2_secrets.py from env vars / ~/.config/agrisense files (CI: GitHub
// secrets), never from the repo, which is public.
#ifndef WM2_AG_PASS
#define WM2_AG_PASS ""
#endif
#ifndef WM2_GR_USER
#define WM2_GR_USER ""
#endif
#ifndef WM2_GR_PASS
#define WM2_GR_PASS ""
#endif
#define AG_HOST "mqtt.agrisenseandcontrol.in"
#define AG_USER "wm2-device"
#define GR_HOST "mqtt.grty.co.in"
#define GR_FARM "girish"
#define BROKER_PORT 1883
// All node settings, saved as /config.json in LittleFS. Changed by commands
// (MQTT, app, web page, SoftAP, serial) and kept across power loss.
#include <Arduino.h>
#include <ArduinoJson.h>

#include "irrigation_core.h"

struct Settings {
  // identity: topics are <farm>/wm2/<zone>/<node>/...
  String farm = GR_FARM;      // fixed
  String zone = "z1";
  String node = "n1";
  // network
  String ssid, pass;
  // MQTT: two fixed broker profiles, one active.
  uint8_t broker = 0;         // 0 = AgriSense, 1 = Girish (customer's own)
  String apPass;              // empty = "wm2-" + last 4 hex of MAC
  int16_t tzMin = 330;        // IST
  // reporting
  uint16_t activeS = 60;      // 30..1800
  uint16_t heartbeatS = 3600; // 60..14400
  uint16_t idleDelayS = 300;
  float psSettleS = 2.0f;
  uint8_t profile = 0;        // 0 responsive, 1 saver, 2 auto (only responsive in this build)
  uint8_t checkMin = 1;
  // valve hardware
  uint16_t pulseMs[2] = {50, 50};
  float gapS = 2.0f;
  bool invert[2] = {false, false};
  bool safeClose = true;      // close valves at critical battery
  uint16_t bypassDefaultMin = 60;
  // flow sensors
  float ppl[4] = {450, 450, 450, 450};  // pulses per litre, set from sensor datasheet
  // battery
  uint8_t chem = 0;           // 0 LiFePO4, 1 lead-acid
  float capAh = 18;
  float vWarn = 12.8f, vSave = 12.5f, vCrit = 12.0f;
  float dailyAh = 0.6f;       // assumed daily use until measured
  // irrigation logic (modes, programs, options)
  wm2core::Cfg core;

  void toJson(JsonObject o, bool secrets) const;
  void fromJson(JsonObjectConst o);  // only keys present are changed
  bool load();
  bool save() const;
  String apPassword() const;
  String hostname() const;  // wm2-a98803e8
  String apSsid() const;    // WM2-A98803E8 (MAC based, so it can be typed as the device ID)
  String base() const;      // agrisense/WM2/WM2_A98803E8/  or  girish/wm2/z1/n1/
  String deviceId() const;  // WM2_A98803E8
  // Active broker profile (all fixed; see top of file)
  const char* brokerHost() const { return broker == 0 ? AG_HOST : GR_HOST; }
  uint16_t brokerPort() const { return BROKER_PORT; }
  const char* brokerUser() const { return broker == 0 ? AG_USER : WM2_GR_USER; }
  const char* brokerPass() const { return broker == 0 ? WM2_AG_PASS : WM2_GR_PASS; }
  static bool agLoginBuiltIn() { return WM2_AG_PASS[0] != 0; }
  static bool grLoginBuiltIn() { return WM2_GR_USER[0] != 0; }
  const char* brokerName() const { return broker == 0 ? "agrisense" : "girish"; }
};

// Program <-> JSON (spec format)
void programToJson(const wm2core::Program& p, JsonObject o);
bool programFromJson(JsonObjectConst o, wm2core::Program& p, uint32_t today, String& err);
const char* modeName(wm2core::Mode m);
bool modeFromName(const char* s, wm2core::Mode& m);

extern Settings cfg;
