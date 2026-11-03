# WM2 WaterMaster firmware 0.1.3 (Girish Farm)

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

One image at **0x0**: `release/WM2-fw-0.1.3-merged-0x0.bin` (gitignored; rebuild with
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
`/api/state` and connects to the one with the matching device ID. "Cloud connected" (MQTT)
still needs the broker login entered on the node (Settings > MQTT) and in the app.

## WiFi and the SoftAP (0.1.2, WM2-004)

While a phone is on the SoftAP the node does not try the farm WiFi; it starts again 30 s
after the last phone leaves. Each try makes the radio hop channels, which dropped the phone
and made scans fail when the saved farm WiFi was out of reach. The boot log line
`[boot] reset reason: ...` (also `reset` in the state) shows a brownout if the supply sags.

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
