#pragma once
#include <Preferences.h>
#include "Config.h"

// Custom display labels only — relay ROLES stay fixed per the spec
// (RL1=pump, RL2=dosing, RL3-RL6=valves 1-4). This just lets the app
// show "Drip Zone A" instead of "Valve 1" everywhere; it has no effect
// on scheduling or the pump auto-follow logic.
class RelayNames {
public:
  String pump = "Pump";
  String dosing = "Dosing";
  String valve[4] = {"Valve 1", "Valve 2", "Valve 3", "Valve 4"};

  void begin() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only open
    pump = prefs.getString("rn_pump", pump);
    dosing = prefs.getString("rn_dosing", dosing);
    for (uint8_t i = 0; i < 4; i++) {
      char key[8];
      snprintf(key, sizeof(key), "rn_v%u", i);
      valve[i] = prefs.getString(key, valve[i]);
    }
    prefs.end();
  }

  void save() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("rn_pump", pump);
    prefs.putString("rn_dosing", dosing);
    for (uint8_t i = 0; i < 4; i++) {
      char key[8];
      snprintf(key, sizeof(key), "rn_v%u", i);
      prefs.putString(key, valve[i]);
    }
    prefs.end();
  }
};
