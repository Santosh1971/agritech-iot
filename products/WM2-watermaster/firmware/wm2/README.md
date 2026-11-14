# WM2 WaterMaster firmware 0.2.7 (Girish Farm)

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

One image at **0x0**: `release/WM2-fw-0.2.7-merged-0x0.bin` (gitignored; rebuild with
`pio run -e wm2` then the merge command in the session notes, or flash from source):

```bash
cd products/WM2-watermaster/firmware/wm2
pio run -e wm2 -t upload
pio device monitor -e wm2
```

Close every serial monitor before flashing. If the tool can't connect: hold BOOT, tap RESET.

## First boot

With no farm WiFi set, the node starts its own WiFi:

- SSID `WM2-<8 hex of the MAC>`; the password is printed on the serial log at boot
  (`SoftAP WM2-... up, password ...`)
- Open **http://192.168.4.1** in the phone browser, or add a node in the app with local
  address `192.168.4.1`
- Settings: set the farm WiFi, then the node shows its farm-WiFi IP on the serial log and
  in Settings > Device. Use that IP in the app afterwards.
- The SoftAP also comes up for 15 minutes after every power-on (kept while a phone is
  connected), even when a farm WiFi is saved.

## LEDs

| LED | Meaning |
|---|---|
| WiFi (blue) | same patterns as FG1: very fast blink (50 ms) = a phone is on the SoftAP; double blink = farm WiFi + MQTT broker; slow blink (1 s) = WiFi without broker, or SoftAP with no phone; fast blink (200 ms) = looking for the farm WiFi |
| Flow 1-4 | one flash per 25 mL, so the blink rate follows the flow rate (max 8 flashes/s) |
| Pressure | on = pressure switch closed (off on a node set to "Not fitted"); blinking = bypass on |
| V1/V2 green/yellow | direction of the last valve pulse (open / close) |

## Finding the node on the farm WiFi (0.1.3, WM2-014)

The node's state carries its device ID (`dev`) and farm-WiFi IP. The app remembers that IP,
and when neither saved address answers it asks every address on the phone's WiFi network for
`/api/state` and connects to the one with the matching device ID. Away from the farm the app uses
the node's MQTT broker with the built-in login (see below).

## WiFi and the SoftAP (0.1.2, WM2-004)

While a phone is on the SoftAP the node does not try the farm WiFi; it starts again 30 s
after the last phone leaves. Each try makes the radio hop channels, which dropped the phone
and made scans fail when the saved farm WiFi was out of reach. The boot log line
`[boot] reset reason: ...` (also `reset` in the state) shows a brownout if the supply sags.

## MQTT broker: AgriSense or Girish only (0.1.7)

The only broker choice is **AgriSense** or **Girish**; nothing else is typed.

| Profile | Host (port 1883) | Login | Topics |
|---|---|---|---|
| AgriSense | mqtt.agrisenseandcontrol.in | `wm2-device` + built-in password | `agrisense/WM2/WM2_<MAC4>/` |
| Girish | mqtt.grty.co.in | built-in Girish user/password, or none if not set | `girish/wm2/<zone>/<node>/` |

`mqtt_config` takes `broker` (`agrisense` | `girish`) plus `zone` and `node` for Girish; any
host/port/user/password sent with it, or saved by an older firmware, is ignored, so a mistyped
login can no longer cut a node off. The device ID always comes from the MAC.

Logins are compiled in, never stored in this (public) repo:

- Local builds: `~/.config/agrisense/wm2_mqtt_pass` (AgriSense), `wm2_girish_user` and
  `wm2_girish_pass` (Girish), or env `WM2_AG_PASS` / `WM2_GR_USER` / `WM2_GR_PASS`.
- Release builds: GitHub secrets `WM2_MQTT_PASS` (required), `WM2_GIRISH_USER`,
  `WM2_GIRISH_PASS` (optional); the app gets the same values via `--dart-define`.
- `get_config` reports only `ag_builtin_login` / `girish_builtin_login`, never the passwords.

## Firmware update over WiFi (0.2.7)

No programmer needed once a node runs 0.2.7 or later (the first such version goes on by USB):

    python3 ../tools/wm2_ota.py 192.168.1.14            # pushes .pio/build/wm2/firmware.bin

`POST /api/ota` with HTTP basic auth `wm2:<WM2_OTA_PASS>` (compiled in by wm2_secrets.py from
env or `~/.config/agrisense/wm2_ota_pass`; CI secret `WM2_OTA_PASS`; without it the endpoint
is disabled). The image goes to the spare OTA slot (default.csv: 2 x 1.25 MB) and the node only
switches after the whole image arrived and its MD5 (header X-MD5) matched, so an interrupted or
corrupt upload leaves the running firmware in place. There is no automatic rollback if new
firmware cannot start: then re-flash by USB. All LEDs blink while an update comes in.
`device_info` reports `ota: true`. Local network only (plain HTTP on the farm WiFi).

## Identify (0.2.6)

`{"cmd":"identify","seconds":20}` (app: the flash icon in the node screen header) blinks every
LED on the board together, 4 times a second, so you can tell which physical board has which
device ID when several are on the same WiFi. Normal LED patterns resume afterwards.

## Program vs manual minutes (0.2.5)

A valve's day program counts only its own (program) minutes; manual runs never count toward
it. The state now also carries `manual_min` / `manual_l` per valve for today (kept across
reboots, reset at the day boundary), and run records (`run`, `last`) say `manual` true/false.
A manual run that carries straight on as a program run (valve never closes) is recorded as two
runs: before, one "15 min" entry hid that 10 of its minutes were manual and did not count.
App: "8/17 min" with "+10 min manual today" on its own line; last runs end "· manual"/"· program".

## LEDs (0.2.4)

The LEDs are driven by their own FreeRTOS task every 20 ms (priority above loop()), so the
blinking never freezes while the main loop waits (broker reconnect, history reads, valve
test). The MQTT socket timeout is 5 s (PubSubClient default 15 s). The app shows a node
reached through the cloud as connected only while the node itself is online (its retained
`status`), not just because the phone reached the broker.

## Commands held by the broker (0.2.2)

The node keeps its MQTT session, so the broker holds commands sent while it was offline
and delivers them on reconnect. Two problems seen on Avinash's node (2026-11-13), fixed:
- A `wifi_forget` from the cloud dropped the WiFi inside the MQTT callback, before the
  message was acknowledged, so the broker re-sent it on every reconnect: the node forgot
  each new farm WiFi 1 s after joining. WiFi changes from MQTT now run after the ack.
- Stale actions (e.g. a "valve_open 10 min" from hours before) ran on reconnect. The app
  stamps `ts` (UTC s); an action from MQTT older than 5 min is refused ("stale command");
  reads still run. Commands without `ts` are not checked.

## Saving the farm WiFi (0.2.1)

Saving WiFi details (app or web page) starts one protected join of up to 25 s: a phone
re-joining the SoftAP no longer cancels it (it did before, so a node set up from the
SoftAP often never joined). The result is in the state: `wifi.join` joining / joined /
failed and `wifi.error` in words (wrong password, network not found - 2.4 GHz only...).
If the saved network is not in the node's last WiFi scan the error is always "network not
found here (out of range, or 5 GHz only)": the radio often reports an auth timeout for a
missing network, which read as "wrong password" (0.2.3). A deliberate disconnect (forget /
WiFi change) now publishes "offline" first; a clean disconnect does not fire the last will.
After a successful join the SoftAP closes 1 min later, even with the phone still on it,
so the phone goes back to the farm WiFi (as on FG1). The WiFi LED now shows the farm WiFi
state (connected / MQTT) ahead of "phone on the SoftAP".

## Zone turns (0.2.0)

One pressure switch per zone shows water in the pipe, but the pressure only feeds some valves
at a time. In the app (zone card > Turns) list the valves in turns; they are saved on the node
with the pressure switch (the **leader**):

`{"cmd":"set_seq","turns":[["WM2_A98803E8:1","WM2_A98803E8:2","WM2_0D4D4FB0:1"],["WM2_11223344:1"]]}`

- With water on, only the current turn's valves may run their programs (each valve keeps its
  own days/minutes). The next turn starts when every valve of the earlier turns has finished
  today (or is not programmed today). Valves not listed run as before; manual opens are never gated.
- Water off mid-turn: the turn pauses (time without water does not count) and carries on
  with the same turn when water is back.
- Nodes talk by UDP broadcast on the farm WiFi, port 47811: leader every 2 s
  (`wm2z`: water, turn, allowed valves, all turns), members every 3 s (`wm2m`: due/done/open).
  No internet or broker needed; no IP addresses to set. Packets also go directly to each
  known peer: the app sends the members' farm-WiFi addresses with `set_seq` (`"ips"`), and
  nodes learn each other's address from every packet. Needed because some routers (seen on
  the office Airtel router, 2026-11-13) do not pass an ESP32's broadcasts to other WiFi
  clients. Best: reserve fixed addresses for the nodes in the router (DHCP reservation); if a
  member's address changes, open Turns in the app and Save again. `zone.udp` in the leader's
  state shows packets sent/failed/received.
- A member stores its leader, so after a reboot it still waits for it. No word from the
  leader for 60 s: its listed valves stay shut and a `zone`/`leader_lost` event is raised.
  A member silent for 90 s is skipped by the leader (`lost`) so the zone is not stuck.
- State: `zone` (leader: turn, water, per-valve `st` waiting/running/finished/lost; member:
  leader, heard_s, gated, allowed) and `turn_wait` on a valve waiting for its turn.
  `zone_leave` makes a member forget its leader; `set_seq` with `[]` stops a leader.
- Logic in `lib/core/src/zone_seq.h` + the core's turn gate, covered by native tests.

## Bench mode (0.1.8)

Valves need 12V on J6 (DRV8871). For bench checks on programmer power,
`{"cmd":"bench","on":true,"minutes":60}` (app: Settings > Bench test) lets valve pulses go out
without 12V and skips the critical-battery safe-close. It lives in RAM only: it ends after the
time limit (1-240 min) or at any reboot, and `bench_s` in the state shows the time left.
LEDs across the valve outputs only blink for the pulse length (Valves > pulse ms, default 50).

## Battery level (0.1.4)

A new battery level (ok / warning / save / critical / over range) must hold for 30 s before
the node reports it, raises the battery event, or closes valves for a critical battery. On a
noisy supply such as the programmer's 5V the raw reading jumps between levels every second.

## Hardware caution (WM2-007/008)

Do not power the board from the programmer's 5V (J9 pin 8) while the 12V bulk capacitors
are fitted. The AP63205 buck runs in forced PWM: with an external 5V slightly above its 5.0 V
setpoint it pushes energy backwards and boosts the 12V rail (seen above 20 V, ADC full scale;
C7/C9/C10 are 25 V parts). Connect only TX/RX/GND and power from J6, or fit a Schottky diode
in series with J9 pin 8 on the next revision. The firmware reports such readings as
`"over_range"` instead of a voltage.

## Pressure switch per node

One pressure switch serves a whole sub-main, so most nodes have none. Settings > Options >
Pressure switch = Not fitted: that node opens valves without the water interlock, judges
"water present" from flow alone (so program minutes still pause when the pump stops), and
never raises the pressure-switch fault.

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
