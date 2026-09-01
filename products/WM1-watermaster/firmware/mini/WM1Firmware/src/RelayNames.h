#pragma once
#include <Preferences.h>
#include "Config.h"

// Custom display labels only — relay/IO ROLES stay fixed per the spec
// (RL1=pump, RL2=dosing, RL3-RL6=valves 1-4, P1/P2=pressure sensors,
// FL=flow/water meter, IN1/IN2=the L1/L2 float switches). This just
// lets the app show "Mirchi" instead of "Valve 3" everywhere; the app
// is what appends the fixed hardware suffix (Mirchi_R3) so the display
// name always carries which physical point it actually is — this
// struct only stores the free-text part the user typed.
class RelayNames {
public:
  String pump = "Pump";
  String dosing = "Dosing";
  String valve[4] = {"Valve 1", "Valve 2", "Valve 3", "Valve 4"};
  String pressure1 = "Pressure 1";
  String pressure2 = "Pressure 2";
  String flow = "Water Meter";
  String waterUpper = "Upper";  // L1 / IN1
  String waterLower = "Lower";  // L2 / IN2

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
    pressure1 = prefs.getString("rn_p1", pressure1);
    pressure2 = prefs.getString("rn_p2", pressure2);
    flow = prefs.getString("rn_fl", flow);
    waterUpper = prefs.getString("rn_wu", waterUpper);
    waterLower = prefs.getString("rn_wl", waterLower);
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
    prefs.putString("rn_p1", pressure1);
    prefs.putString("rn_p2", pressure2);
    prefs.putString("rn_fl", flow);
    prefs.putString("rn_wu", waterUpper);
    prefs.putString("rn_wl", waterLower);
    prefs.end();
  }
};
