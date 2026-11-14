#pragma once
// Zone turns over the farm WiFi.
//
// One pressure switch (on the zone "leader" node) shows that water is in the
// zone's pipe, but the pressure is only enough for some valves at a time. The
// leader holds the turns ([["WM2_A:1","WM2_B:1"],["WM2_C:2"]]): while it sees
// water it lets only the valves of the current turn run their programs, and
// moves to the next turn when every valve of the earlier turns has finished
// today. Water off = the turn pauses; it carries on when water is back.
//
// Leader and members talk by UDP broadcast on the farm WiFi (no internet, no
// broker, no IP addresses to type):
//   leader -> all every 2 s: {"t":"wm2z","ld":ID,"w":1,"turn":N,"al":[allowed],"tu":[turns]}
//   member -> all every 3 s: {"t":"wm2m","dev":ID,"ld":LEADER,"v":[[due,done,open],[...]]}
// A member learns from "tu" that it is listed and stores the leader (so after
// a reboot it still waits for the leader). A member that has not heard its
// leader for 60 s keeps its listed valves shut and raises an event.
#include <ArduinoJson.h>

#include "irrigation_core.h"

namespace zone {

// Zone turns built in (-DWM2_ZONE_TURNS=1); off by default: independent nodes.
bool enabled();

// Every tick, before core.tick (under Lock): which program valves are gated
// and which of them may run now.
void gate(wm2core::Inputs& in);
// From the main loop (under Lock): UDP receive, leader decisions, broadcasts.
void loop();
// set_seq command (leader): turns as an array of arrays of "WM2_XXXXXXXX:1|2".
// Empty array = stop being a leader. Returns false with `err` if malformed.
// `ips`: optional {"WM2_...":"192.168.1.20"} from the app, to reach members directly.
bool setTurns(JsonVariantConst turns, JsonVariantConst ips, String& err);
// zone_leave command (member): forget the leader.
void leave();
// "zone" object in the node state (nothing when not part of zone turns).
void toJson(JsonObject o);

}  // namespace zone
