# FG1 Polyhouse — misting controller (Belgaum rose polyhouse)

FG1 Controller at the pump panel (relay + T/RH sensor) plus FG1 Sensor Nodes
(same board, no relay) that report over ESP-NOW. One controller per
polyhouse and pump. The **ASAC FG1** Android app controls it over the
controller's own WiFi, the farm WiFi, or the internet (MQTT).

```
firmware/   PlatformIO: env controller, env node, env native (logic tests)
app/        Flutter app "ASAC FG1" (Android + web demo)
```

## Build and flash

Credentials are not in git (public repo): copy `firmware/include/Secrets.example.h`
to `Secrets.h` and `app/lib/services/secrets.example.dart` to `secrets.dart`, and
fill in the real values (same MQTT login as FG1).

```bash
cd products/FG1-flowguard/polyhouse/firmware
pio test -e native                     # 19 misting-rule tests, no board needed
pio run -e controller -t upload --upload-port /dev/cu.usbserial-XXXX
pio run -e node -t upload --upload-port /dev/cu.usbserial-YYYY
pio device monitor -b 115200
```

App: `cd app && flutter build apk --release` (APK in `build/app/outputs/flutter-apk/`).
The app has a **demo polyhouse** (Add polyhouse → "Add a demo polyhouse") that
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
| Serial | `status`, `events`, `pump on`, `pump off`, `auto` | `status`, `forget`, `send` |

**Misting rules** (`firmware/lib/MistLogic`, all values adjustable in the app):
stop if T ≤ 29 °C or RH ≥ 60 % (stop always wins); start if T ≥ 32 °C, or
RH ≤ 45 % with T > 29 °C; in between the last decision holds. While misting:
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
other polyhouse, remove it in the app, or hold BOOT for 5 s.

**MQTT**: `agrisense/FG1/FG1P_xxxx/{status,resp,lwt,command,settings}` on
mqtt.agrisenseandcontrol.in with the shared FG1 login. Note that
`agrisense/FG1P/...` is blocked by the broker ACL (tested 2026-10-31), hence
the FG1 tree. `settings` is retained, so a change made from the internet
while the controller is offline is applied when it reconnects (only if
newer, by its `rev`).

## Bench test with two boards (tomorrow)

1. **Logic**: `pio test -e native`. All 19 tests should pass.
2. **Flash** board A as controller and board B as node. On each boot log,
   check the sensor line, e.g. `[TH] SHT4x at 0x44`. If it says
   `No T/RH sensor found`, the I2C scan lists what is on the bus; tell me
   the part.
3. **Controller alone**:
   - Phone joins `FG1P_xxxx` (password on the label).
   - In the app: Add polyhouse → Find controller → Save.
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
   - The app keeps working over the farm WiFi via the LAN IP: set the address under Settings → Polyhouse name and address.
   - It also works over the internet: switch the phone to mobile data and the chip shows "Internet".
8. **Range**, for polyhouse 2 at about 45 m:
   - Walk the node away from the controller and watch signal (dBm) and "% delivered" on the Sensors tab.
   - Below about −85 dBm, or under 90 % delivered, plan an external antenna or a relay node.

## Known limits / to decide

- Trend (24 h) and events live in RAM and restart after a power cut. Settings,
  mode, sensors and WiFi are kept.
- While the controller is on its own SoftAP and has a farm WiFi saved that
  it cannot reach, it retries every 60 s. During the ~8 s retry, node
  readings can be missed, and the nodes retry by themselves.
- MQTT uses the shared FG1 credential on port 1883, which is the same deferred
  security trade-off as FG1 and WPC.
- APK is signed with the debug key, which is fine for side-loading. Play Store would need the release key.
