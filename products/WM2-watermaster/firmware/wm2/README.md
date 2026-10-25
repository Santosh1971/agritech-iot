# WM2 WaterMaster firmware 0.1.0 (Girish Farm)

Phase 2 "node core" from the spec (Claude Doc *WM2 WaterMaster: Girish Farm Functional
Specification*, https://claude.ai/artifact/NSR2Czwmsft66fzP3ZnTnU).

**In this build:** valve control with the water interlock and bypass, day-wise programs
(minutes or litres) with power-cut pause and day results, manual runs, sequential V1/V2,
water-loss keep/close, never-close relief valve, flow counting (hardware pulse counter),
learned normal flow with low/high/imbalance/leak and pressure-switch-fault alarms, battery
monitor and days remaining, on-device history (raw records, events, day results, CSV
export), MQTT to any broker (state, telemetry, heartbeat, events, commands, store and
forward), built-in web page, SoftAP, serial console.

**Not yet (phase 3):** Saver profile (deep sleep + ULP flow counting), hourly history tier,
firmware update over WiFi, measured daily battery use.

## Flash

One image at **0x0**: `release/WM2-fw-0.1.0-merged-0x0.bin` (gitignored; rebuild with
`pio run -e wm2` then the merge command in the session notes, or flash from source):

```bash
cd products/WM2-watermaster/firmware/wm2
pio run -e wm2 -t upload
pio device monitor -e wm2
```

Close every serial monitor before flashing. If the tool can't connect: hold BOOT, tap RESET.

## First boot

With no farm WiFi set, the node starts its own WiFi:

- SSID `WM2-z1-n1`, password `wm2-xxxx` (last 4 hex digits of the MAC, printed on the
  serial log at boot: `SoftAP WM2-z1-n1 / wm2-xxxx`)
- Open **http://192.168.4.1** in the phone browser, or add a node in the app with local
  address `192.168.4.1`
- Settings: set the farm WiFi, then the node shows its farm-WiFi IP on the serial log and
  in Settings > Device. Use that IP in the app afterwards.
- Double-press RESET within 5 s to bring SoftAP back for 15 minutes.

## MQTT broker: AgriSense or Girish

Settings > MQTT broker picks one; each keeps its own host, port and login.

| | AgriSense (default) | Girish |
|---|---|---|
| Host | `mqtt.agrisenseandcontrol.in:1883` (preset) | his broker, entered on site |
| Topics | `agrisense/WM2/WM2_xxxxxxxx/...` (AgriSense convention, ID from the MAC) | `girish/wm2/z1/n1/...` (farm/zone/node) |
| Login | a WM2 account on the AgriSense broker (not in this public repo; enter it on the node) | his user/password |
| Zone-wide commands | no (per-device access rules) | `girish/wm2/z1/all/cmd`, `girish/wm2/all/cmd` |

Sub-topics are the same on both: `state` (retained), `status` (online/offline, last will),
`telemetry`, `heartbeat`, `event`, `cmd`, `resp`. The AgriSense broker only lets each account
use its own topics: the FG1 device account cannot publish under `agrisense/WM2/`, so a WM2
account must be added on the VPS. The AgriSense dashboard bridge also needs `WM2` added to its
product list before WM2 data shows in the web app.

Serial console (115200): type `h` for help, `s` for the state, or any JSON command, e.g.
`{"cmd":"valve_open","valve":1,"minutes":2}`.

## Bench test plan (25 Oct 2026)

Use a 12 V supply or the LiFePO4 on J6 for steps 4 onward (valves need 12 V; on USB they
show "pulse pending").

| # | Do | Expect |
|---|---|---|
| 1 | Flash, open monitor | `=== WM2 WaterMaster fw 0.1.0 ===`, RTC time, SoftAP line with password, a `[st]` status line every 10 s |
| 2 | Phone joins `WM2-z1-n1`, open 192.168.4.1 | Status page loads; clock set from the phone automatically if the RTC was unset |
| 3 | Settings > Farm WiFi, save | Serial: `WiFi up, IP ...`; page reachable at that IP from the office WiFi |
| 4 | 12 V on J6 | Battery shows ~12-14 V; both valves pulse CLOSED at boot (resync), 2 s apart |
| 5 | Valve 1 Open 2 min, no water | V1 "waiting for water", no pulse |
| 6 | Jumper J1 (pressure switch closed) | After 2 s: Water YES, PS LED on, V1 opens (green LED flash, serial `V1 OPEN pulse`) |
| 7 | Remove jumper (power cut) | V1 stays open (keep-open default); minutes stop counting |
| 8 | Jumper back, wait | V1 finishes its 2 min and closes; event "manual_done" |
| 9 | Bypass on 5 min, no jumper, open V2 | V2 opens without water; PS LED blinks |
| 10 | Programs: V1 today, 3 min, Save; jumper on | V1 runs 3 min then closes, reason `program_done` |
| 11 | Short Flow 1 signal to GND a few times / spin a sensor | L/min and litres rise on Flow 1; Flow 1 LED flashes |
| 12a | Settings > MQTT broker = AgriSense, enter the WM2 account | `[mqtt] connected to agrisense broker ...`; `agrisense/WM2/WM2_xxxxxxxx/state` appears (`mosquitto_sub -h mqtt.agrisenseandcontrol.in -u ... -P ... -t 'agrisense/WM2/#' -v`) |
| 12b | Switch to Girish, enter his details | `[mqtt] connected to girish broker ...`; `girish/wm2/z1/n1/state` appears |
| 13 | Publish `{"cmd":"get_state"}` to `<base>cmd` | Reply on `<base>resp` |
| 14 | History tab: Today | Records and totals; CSV download opens |
| 15 | App (WM2-app-0.1.0.apk): add node with the IP, repeat 5-10 | Same behaviour from the app |
| 16 | Reboot during a program run | Run resumes with today's minutes kept |

Report any step that differs, with the serial log around it.

## Layout

- `lib/core/` irrigation logic, no Arduino code; `pio test -e native` runs 13 tests on the PC
- `src/hw.cpp` RTC, valves, flow (PCNT), pressure, battery, LEDs
- `src/history.cpp` LittleFS history; `src/net.cpp` WiFi/SoftAP/MQTT; `src/web.cpp` + `webpage.h`
- `src/commands.cpp` one command set for MQTT, app, web, serial; `src/main.cpp` the loop
- `../tools/node_sim.py` simulator of a node (page + commands) for app work without hardware
