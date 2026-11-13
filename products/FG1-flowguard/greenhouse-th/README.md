# FG1 Greenhouse TH — greenhouse temperature/humidity monitoring and misting control

First site: Belgaum rose polyhouse.

FG1 Controller at the pump panel (relay + T/RH sensor) plus FG1 Sensor Nodes
(same board, no relay) that report over ESP-NOW. One controller per
greenhouse and pump. The **ASAC TH** Android app controls it over the
controller's own WiFi, the farm WiFi, or the internet (MQTT).

```
firmware/   PlatformIO: env controller, env node, env native (logic tests)
app/        Flutter app "ASAC TH" (Android + web demo)
```

## Build and flash

Credentials are not in git (public repo): copy `firmware/include/Secrets.example.h`
to `Secrets.h` and `app/lib/services/secrets.example.dart` to `secrets.dart`, and
fill in the real values (same MQTT login as FG1).

```bash
cd products/FG1-flowguard/greenhouse-th/firmware
pio test -e native                     # 24 misting-rule tests, no board needed
pio run -e controller -t upload --upload-port /dev/cu.usbserial-XXXX
pio run -e node -t upload --upload-port /dev/cu.usbserial-YYYY
pio device monitor -b 115200
```

**ESP32-C3 Super Mini carrier** (the ASC Mini-C3 board, the FG1 boards delivered on 2026-11-01): use
`controller_c3` / `node_c3`. They appear as `/dev/cu.usbmodem*` (native USB), not `usbserial`.
Pins: relay IO20, I2C SDA 6 / SCL 7, blue LED IO8 = WiFi status, red STATUS LED IO10 = flash on
each reading, BOOT IO9. The radio is capped at 8.5 dBm, because at full power the Super Mini's
antenna is too distorted for the other board to hear it. Even so, RSSI was only −70…−92 dBm side by side, so check range early.

Controller bench commands (serial monitor): `cmd {"cmd":"node_add","mac":"48F6EE126AF8"}`
(any app command), `i2c` (20× bus scan), `i2cswap` (scan with SDA/SCL exchanged), `rtc`
(raw DS1307 registers), `settime <IST wall-clock unix>`, `gpio <pin> [0|1]`, `sensor` (re-detect),
`manual`, `auto`, `pump on|off`, `status`, `events`.

**No sensor yet? `sim <T> <RH>`** on the node's (or the controller's) serial port, e.g. `sim 33 40`,
makes its sensor report that value; the app shows the sensor as `SIM`. `sim off` (or a reboot) goes
back to the real sensor; it is never saved. Use it to bench-test Auto: `sim 33 40` starts misting
(after the 3 min lockout following a power-up), `sim 28 40` or `sim 33 65` stops it, `sim off` gives
the sensor-fault stop. The controller smooths readings, so a sudden change takes ~30-40 s to act.

The DS1307 needs a CR2032, or its VBAT tied to GND; with VBAT floating it answers only some of the time.
Plug the T/RH stick in with the power off. The AHT20 datasheet (§5.1) warns that it can start up
dead if SDA/SCL are powered before VDD.

**Release images for people without PlatformIO** (e.g. Avinash): one merged file per board, flashed at
0x0 (esptool-js in Chrome, or `esptool.py --chip esp32c3 write_flash 0x0 <file>`). Make them with
`esptool.py --chip esp32c3 merge_bin -o <out>.bin --flash_mode keep --flash_freq keep --flash_size keep
0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin`. Note that the
merged file fills 0x9000-0xDFFF (NVS) with 0xFF, so **flashing it wipes the pairing, settings and farm
WiFi** (checked 2026-11-13): fine for a first install; to update a board in use, flash only
`firmware.bin` at 0x10000. Release files contain the credentials: keep them in `release-*/` (git-ignored).

App: `cd app && flutter build apk --release` (APK in `build/app/outputs/flutter-apk/`).
The app has a **demo greenhouse** (Add greenhouse → "Add a demo greenhouse") that
simulates a controller, for showing the customer without hardware.

## How it works

| | Controller (`FG1P_xxxx`) | Sensor Node (`FG1N_xxxx`) |
|---|---|---|
| Relay | GPIO19 → ULN2003 → relay → starter coil | not used (held OFF) |
| T/RH | auto-detects SHT4x / SHT3x (0x44/45), AHT20 (0x38), HTU21/SHT21 (0x40) on SDA21/SCL22 | same |
| Clock | DS1307 (0x68); else NTP / phone | — |
| Network | farm WiFi + MQTT, SoftAP fallback `FG1P_xxxx` (password on the label) at 192.168.4.1 | ESP-NOW only, follows the controller's channel |
| LED GPIO2 | slow blink = SoftAP idle, fast = phone on SoftAP, double = internet OK | slow = not added yet, fast = searching for controller, double = OK |
| LED GPIO32 | flash on each sensor reading received | flash on each reading the controller ACKs |
| BOOT 5 s | forget farm WiFi → SoftAP | forget controller (back to "new sensor") |
| Serial | `status`, `events`, `pump on`, `pump off`, `auto`, `sim T RH` | `status`, `forget`, `send`, `sim T RH` / `sim off` |

**Misting rules** (`firmware/lib/MistLogic`, all values adjustable in the app):
stop if T ≤ 29 °C or RH ≥ 60 % (stop always wins); start if T ≥ 32 °C, or
RH ≤ 45 % with T > 29 °C; in between the last decision holds. Restart margins (app: Settings →
Restart margin, fw 0.1.3+): after a humidity stop misting restarts only once RH <= stop − margin
(60 − 3 = 57 %), after a temperature stop only once T >= stop + margin (29 + 1 = 30 °C), so a value
hovering at a stop line cannot cycle the pump. Saved settings from older firmware carry over on
update (the margins start at 1 °C / 3 %). While misting:
2:00 ON / 3:00 OFF pulses, only inside the working hours (11:00–15:00).
Motor protection: at least the OFF time between any two starts (a power cut
counts as an OFF, so the pump never restarts within 3 min of power coming
back), and max 15 starts per hour. Sensor reading older than 2 min → stop.
Manual ON stops by itself after 30 min and returns to Auto. After a power cut
the controller comes back in Auto (or Off, if it was Off).

**Control input**: tick "Use for control" on one sensor, or on several to
average them. With none ticked, the controller's own sensor (the "Outside"
reference in the pump panel) is used, which is handy during commissioning.

**Adding a sensor**: power the node. Within about 10 s it shows under
Sensors → New sensors found. Tap Add. The node stores its controller and
keeps working after power cuts. Remove releases it. To move a node to the
other greenhouse, remove it in the app, or hold BOOT for 5 s.

**MQTT**: `agrisense/FG1/FG1P_xxxx/{status,resp,lwt,command,settings}` on
mqtt.agrisenseandcontrol.in with the shared FG1 login. Note that
`agrisense/FG1P/...` is blocked by the broker ACL (tested 2026-10-31), hence
the FG1 tree. `settings` is retained, so a change made from the internet
while the controller is offline is applied when it reconnects (only if
newer, by its `rev`).

## Bench test with two boards (tomorrow)

1. **Logic**: `pio test -e native`. All 24 tests should pass.
2. **Flash** board A as controller and board B as node. On each boot log,
   check the sensor line, e.g. `[TH] SHT4x at 0x44`. If it says
   `No T/RH sensor found`, the I2C scan lists what is on the bus; tell me
   the part.
3. **Controller alone**:
   - Phone joins `FG1P_xxxx` (password on the label).
   - In the app: Add greenhouse → Find controller → Save.
   - Status should show the Outside reading.
   - The clock sets itself from the phone on connect.
   - Inside (control) shows the Outside value with the note "Using outside sensor (no sensor chosen)".
4. **Pair the node**:
   - Sensors → New sensors found → Add.
   - The node log should show `Paired with FG1P_xxxx`.
   - The node card should show T/RH, signal, and "% delivered".
   - The first node added becomes the control input on its own.
   - Use "Flash its lights" to check that the right box blinks.
5. **Relay**: put a lamp or meter on the relay, not the starter, for the bench.
   - Manual → Pump ON / OFF.
   - Auto with thresholds moved around room conditions: for example, set start temp below room temp and the window to include now, then watch 2:00 ON / 3:00 OFF.
   - Warm the node's sensor with a hand to raise RH, or breathe on it; RH ≥ stop RH must cut the pump at once.
6. **Faults**:
   - Unplug the node. After 2 min Status shows "Sensor fault — misting stopped" and the pump goes OFF.
   - Plug it back in. It reconnects without touching anything.
   - Power-cycle the controller while misting. The pump must stay OFF for 3 min, then resume in Auto.
7. **Farm WiFi**:
   - Settings → Farm WiFi → Set up.
   - The controller joins the router. After about 1 min of stable WiFi its own SoftAP turns off.
   - The node follows the router's WiFi channel by itself. Expect up to about 30 s of "searching" on the node.
   - The app keeps working over the farm WiFi via the LAN IP: set the address under Settings → Greenhouse name and address.
   - It also works over the internet: switch the phone to mobile data and the chip shows "Internet".
8. **Range**, for greenhouse 2 at about 45 m:
   - Walk the node away from the controller and watch signal (dBm) and "% delivered" on the Sensors tab.
   - Below about −85 dBm, or under 90 % delivered, plan an external antenna or a relay node.

## Known limits / to decide

- Star version: nodes reach the controller directly (no hopping). **Repeater version** (0.2.0+, envs
  `controller_c3_rpt` / `node_c3_rpt`): a far node may go through one nearer node; see
  [docs/repeater-plan.md](docs/repeater-plan.md). Star and repeater builds work together, but
  repeaters need the repeater controller.

- Trend (24 h) and events live in RAM and restart after a power cut. Settings,
  mode, sensors and WiFi are kept.
- While the controller is on its own SoftAP and has a farm WiFi saved that
  it cannot reach, it retries every 60 s. During the ~8 s retry, node
  readings can be missed, and the nodes retry by themselves.
- MQTT uses the shared FG1 credential on port 1883, which is the same deferred
  security trade-off as FG1 and WPC.
- APK is signed with the debug key, which is fine for side-loading. Play Store would need the release key.
