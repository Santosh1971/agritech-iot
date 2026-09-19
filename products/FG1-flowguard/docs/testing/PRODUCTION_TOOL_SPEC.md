# FG1 Production Flash/Test/Report Tool — Specification v1

> **Superseded by [`PRODUCTION_TOOL_SPEC_V2.md`](PRODUCTION_TOOL_SPEC_V2.md).**
> This v1 architecture (Flutter app + bench laptop "Flash Bridge" +
> ESP8266 WiFi jig + hotspot/travel-router test AP) was never built as
> described. What's actually running on the bench is a phone-only
> native Android app, a USB-serial (not WiFi) jig, and the office WiFi
> router as the only test AP — no laptop, no hotspot. Read v2 instead;
> kept here for history only.

Status: Draft — approved architecture, not yet built. Supersedes the
transport-layer parts of [`TEST_JIG_SPEC.md`](TEST_JIG_SPEC.md) (jig now
talks WiFi, not USB-serial); that document's Tier 1/Tier 2 **test step
content** (§5, §6) is still the procedural reference this tool
implements — read this doc for architecture, that one for the test
checklist.

## 1. Purpose

Avinash is assembling ~10 units of FG1. Each unit needs, on the bench:
flash firmware → verify it boots clean → verify every subsystem
(relay, flow sensor, WiFi, MQTT, RTC) actually works on *this specific
assembled board* → produce a pass/fail record the shop can point to if
a customer reports a defect later.

Today this exists as a set of real, runnable Python scripts
(`testing/*.py`) driven from a terminal on a laptop. This spec turns
that into a **phone-operated tool**: one operator, one phone app, one
laptop that mostly sits closed except to answer flash requests.

## 2. Roles

- **Operator** (Avinash or whoever is on the bench) — uses only the
  phone. Never touches a terminal.
- **Flash Bridge** — a small background service on a bench laptop.
  Set up once, then left running for the whole production run.
- **Test Jig** — a permanent fixture MCU (ESP8266) that injects flow
  pulses and senses the relay output. Built once, reused for every
  unit and every future production run.

## 3. Why not "phone does everything," including flashing

Flashing a blank ESP32 requires talking to its ROM bootloader over a
real serial link (DTR/RTS strapping into download mode, then the
esptool sync/stub/flash protocol) — that needs a USB connection, not
WiFi. Two ways to get a phone to do that directly were considered and
rejected for v1:

- **Browser-based WebSerial flashing** (Espressif's `esp-web-tools`) —
  real and Android-Chrome-capable, but depends on Android's WebSerial+
  USB-OTG stack behaving with whatever USB-UART chip (CP2102/CH340) is
  on the dev board, which is not something to gamble a live production
  line on for a first version.
- **Native Android esptool reimplementation** — highest effort, highest
  risk (bootloader sync timing, stub upload, flash-mode quirks), for a
  10-unit run that doesn't need it.

Decision: keep a **laptop in the loop for the USB flash step only**
(this matches your own choice above). Everything after a successful
flash — functional test, jig control, report — is phone + WiFi, no
laptop involved.

## 4. System architecture

```
                        ┌─────────────────────────┐
                        │   Bench LAN (WiFi AP)    │   ← existing "test AP"
                        │  e.g. old phone hotspot  │     from TEST_JIG_SPEC §2.5
                        └───────────┬─────────────┘
                                    │
                 ┌──────────────────┼───────────────────┐
                 │                  │                    │
         ┌───────▼───────┐  ┌───────▼────────┐   ┌───────▼────────┐
         │ Flash Bridge   │  │ Operator Phone │   │  MQTT Broker   │
         │ (bench laptop) │◄─┤ (Flutter app)  │   │ (real, staging │
         │  USB─┐         │  │                │   │  or prod)      │
         └──────┼─────────┘  └───────┬────────┘   └────────────────┘
                 │ USB                │ WiFi (switches network,
         ┌───────▼────────┐           │  see §5 session flow)
         │   DUT (ESP32)  │◄──────────┘
         │  FG1 board     │
         └───┬────────┬───┘           ┌────────────────┐
             │ GPIO35 │ relay output   │   Test Jig      │
             │(pigtail)│ (screw term.) │  (ESP8266)      │
             └────────┴───────────────►│  joins DUT's    │
                                       │  own SoftAP as   │
                                       │  a station once  │
                                       │  DUT boots       │
                                       └──────────────────┘
```

Three separate WiFi actors, two networks, one operator holding a phone
that switches between them at one well-defined point in the sequence.
This is not new complexity introduced by this tool — it is exactly the
same DUT-boots-into-its-own-SoftAP model the consumer app already
handles in [`local_setup_screen.dart`](../../mobile-app/flutter_app/lib/screens/local_setup_screen.dart)
("the phone has to actually join the device's WiFi network first...
this screen guides that step"). This tool reuses that exact UX pattern,
already proven in the shipped app, rather than inventing a new one.

## 5. Session flow (per unit)

| # | Step | Phone's network | Talks to |
|---|---|---|---|
| 1 | Plug DUT into laptop via USB. Tap "Flash" in app. | Bench LAN | Flash Bridge (HTTP) |
| 2 | Flash Bridge runs `pio run -t upload`, streams result | Bench LAN | Flash Bridge |
| 3 | Flash Bridge tails boot log 10s, checks markers (§7 of TEST_JIG_SPEC), returns device ID | Bench LAN | Flash Bridge |
| 4 | App shows: **"Join WiFi network `<DEVICE_ID>` now"** with a button that opens system WiFi settings | — (switching) | — |
| 5 | Operator taps the SSID in the system WiFi picker (password pre-filled/known, see §9.1) | DUT's own SoftAP | — |
| 6 | App auto-reconnects to `ws://192.168.4.1/ws`, runs `factory_reset`, `calibrate` | DUT SoftAP | DUT (WS) |
| 7 | Jig has already auto-joined this same SoftAP within a few seconds of it appearing (§6.2) — app confirms via jig `/ping` | DUT SoftAP | Jig (HTTP) |
| 8 | `relay_test` command to DUT + jig `/relay` poll to confirm physical switch | DUT SoftAP | DUT (WS) + Jig (HTTP) |
| 9 | Jig `/pulse?n=450`, then poll DUT `device_info.liters_delivered` | DUT SoftAP | DUT (WS) + Jig (HTTP) |
| 10 | `wifi_config` pointing DUT's STA side at the Bench LAN; poll `device_info` for `wifi_connected` + `mqtt_connected` (DUT runs AP+STA simultaneously, so the phone never has to leave the DUT's SoftAP for this — see [`dut_client.py`](../../testing/dut_client.py) header comment) | DUT SoftAP (unchanged) | DUT (WS) |
| 11 | RTC sanity from the same `device_info` payload | DUT SoftAP | DUT (WS) |
| 12 | `factory_reset` — ship clean | DUT SoftAP | DUT (WS) |
| 13 | App generates the report, appends to the run log | — | local storage |
| 14 | Operator unplugs unit, moves to next one — app prompts to rejoin Bench LAN before the next Step 1 | back to Bench LAN | — |

Everything from step 6 onward is one continuous phone-on-DUT's-own-AP
session — only steps 1–3 (needs Flash Bridge) and step 10's
verification target (Bench LAN) sit on the other network, and step 10
doesn't require the *phone* to be there, only the DUT.

## 6. Test Jig (ESP8266)

### 6.1 Hardware
Flow sensor pigtail into a pulse-output GPIO (D1/GPIO5 → DUT GPIO35),
same as originally drafted, plus a common GND jumper between the jig
and DUT boards (required for the pulse signal to register at all).

**Relay sense — revised from the original TEST_JIG_SPEC.md §2.4 design**
during first bench bring-up (2026-09-07): that draft used a resistor
divider off a powered "relay output" node, sized for an assumed 12V
supply — a real risk of exceeding the ESP8266 A0 pin's ~3.2V limit if
built with a different supply voltage, and it needs re-deriving the
divider ratio every time the supply changes. Replaced with a dry
contact off the relay's own mechanical switch instead: **COM → jig
GND, NO → D2/GPIO4, read via internal pull-up** (open = HIGH = relay
off; closed to GND = LOW = relay on). No external voltage, resistors,
or LED needed for the sense path at all — simpler to build and
supply-voltage-agnostic. D2 was chosen over D3/D4/D8 specifically
because those pins have boot-time strapping requirements a relay
contact's power-on state could interfere with. See the jig firmware's
header comment for the authoritative wiring reference.

Same optional power-cycle MOSFET as originally drafted. Only the jig's
own MCU and its link to the outside world changes otherwise:

| | TEST_JIG_SPEC.md (draft) | This spec |
|---|---|---|
| MCU | Arduino Nano or ESP8266 | ESP8266 (e.g. Wemos D1 Mini) |
| Link to controller | USB-serial to a PC | WiFi (station mode) |
| Protocol | Line-based serial commands | HTTP JSON (see §6.3) |

LED photosensors (`WIFI_LED_SENSE_PIN`, `FLOW_LED_SENSE_PIN` in the
draft `.ino`) are **dropped for v1** — ESP8266 has exactly one analog
input (A0), already spoken for by the relay-sense divider. This matches
the existing PT spec's own fallback (§6 step 9 of TEST_JIG_SPEC.md): a
2-second human glance at the WiFi/flow LEDs during the relay and WiFi
steps. Revisit only if a fully hands-off line later justifies an I2C
ADC expander.

### 6.2 Auto-join logic
On boot (and after being idle with no DUT network in range), the jig:
1. Scans for APs whose SSID matches the DUT's known prefix (the
   product's `DEVICE_ID` scheme, e.g. `FG1_*` — confirm exact prefix
   with firmware before building).
2. Joins the strongest match using the SoftAP's current fixed password
   (`SOFTAP_PASSWORD` in [`Config.h`](../../firmware/include/Config.h) — **today this is
   a single hardcoded value (`"water1234"`) shared by every unit**; see
   §9.1, this is exactly what the jig's auto-join depends on).
3. Registers itself via mDNS as `fg1jig.local` so the phone app never
   needs a manually-typed IP.
4. If the DUT's SoftAP disappears (unit swapped, or DUT joined the
   Bench LAN and dropped its AP), goes back to scanning.

This means only **one unit should be powered on the bench at a time** —
if two DUTs' SoftAPs are both up, the jig's "strongest match" join is
ambiguous. Acceptable for a single-station, single-operator line; call
this out on the jig's physical label.

### 6.3 Jig HTTP API (replaces §4's serial protocol)

| Request | Response | Meaning |
|---|---|---|
| `GET /ping` | `200 {"ok":true}` | Liveness + confirms jig has joined this unit's SoftAP |
| `POST /pulse?n=<count>` | `200 {"ok":true,"emitted":<count>}` | Emit exactly `n` pulses on the flow-sim output, block until done, then respond |
| `GET /relay` | `200 {"state":"on"\|"off"}` | Sensed state of the relay indicator circuit |
| `GET /status` | `200 {"joined_ssid":"...","rssi":-52}` | Debug/diagnostics — which DUT it thinks it's attached to |

Kept intentionally as small and stateless as `jig_controller.ino`
already is — this is a protocol/transport change, not a logic change.

## 7. Flash Bridge (laptop service)

A thin wrapper the phone calls over the Bench LAN — not a rewrite of
`flasher.py`/`serial_monitor.py`, a thin HTTP shim around them.

| Endpoint | Behavior |
|---|---|
| `GET /health` | `{"ok":true,"pio_found":true,"port":"/dev/cu.usbserial-0001"}` — app shows this before letting the operator start |
| `POST /flash {"env":"esp32dev"}` | Runs `flasher.flash()`; streams stdout/stderr as it goes (chunked response or WebSocket) so the app can show live progress, not just a spinner |
| `GET /boot_log?window_s=10` | Runs `serial_monitor.capture_boot_log()`, returns the same dict already defined in that module (`passed`, `device_id`, `markers_seen`, `unexpected_errors`, `raw_log`) |

No new logic — this is the existing, already-correct Python code from
`testing/flasher.py` and `testing/serial_monitor.py`, given an HTTP
face. Runs on plain Flask/FastAPI, one port picked at setup and printed
on the laptop screen once (e.g. `http://192.168.1.50:8787` — put this
in the app's settings screen as a one-time-per-session field, or
discover it via mDNS the same way the jig is discovered).

Auth: none — the Bench LAN is a dedicated, isolated test network with
nothing else on it (per TEST_JIG_SPEC §2.5). Do not put this bridge on
any network that also carries production customer traffic.

## 8. Mobile App

### 8.1 Where it lives
**A separate small Flutter app** ("FG1 Production Tester"), not a mode
bolted onto the consumer app. Reasoning: production tooling (raw
`factory_reset`/`calibrate`/`relay_test` commands, jig control, flash
triggers) has no business shipping in a build a customer could
sideload, and keeping it separate means it can be blunt/utilitarian
(no polish needed) and iterated on independently of consumer app
releases. It can still share code: pull `local_service.dart`'s WS
client and the `DeviceStatus`/model classes into a small shared Dart
package, rather than duplicating them.

### 8.2 Screens
1. **Setup check** — pings Flash Bridge `/health` and jig `/ping` (if
   already joined from a prior unit), shows two green/red dots before
   allowing a run to start.
2. **Flash & Boot** — big "Flash" button → live log stream from
   `/flash` → boot log check result. Device ID surfaces here the moment
   it's parsed.
3. **Join Device WiFi** — the `local_setup_screen.dart`-style prompt
   from step 4/5 of §5, with the target SSID shown large (and QR-code
   optional stretch goal, see §12).
4. **Running Tests** — a checklist mirroring TEST_JIG_SPEC.md §6 steps
   3–8 (factory reset, calibrate, relay, flow, WiFi+MQTT, RTC), each
   row live-updating ⏳ → ✅/❌ as it completes, matching the 90-second
   target from that spec.
5. **Report** — pass/fail summary for this unit, "Save & Next Unit"
   button (triggers `factory_reset` + prompts rejoin Bench LAN).
6. **Run History** — list of every unit tested this session/all-time,
   tap one to re-view its report, export-all-as-CSV button.

### 8.3 New service classes (mirrors `local_service.dart`'s pattern)
- `FlashBridgeService` — HTTP client for §7's endpoints.
- `JigService` — HTTP client for §6.3's endpoints.
- `ProductionTestRunner` — orchestrates the step sequence (a direct
  Dart port of `test_production.py`'s logic), reusing `LocalService`
  as-is for every DUT command (it already speaks the right protocol).

### 8.4 Report data model (per unit)
```json
{
  "device_id": "FG1_A1B2C3",
  "firmware_version": "1.0.0",
  "timestamp_utc": "2026-09-07T10:15:00Z",
  "operator": "Avinash",
  "station": "bench-1",
  "steps": {
    "flash": {"passed": true},
    "boot_log": {"passed": true, "markers_seen": {...}},
    "factory_reset": {"passed": true},
    "calibrate": {"passed": true, "ppl": 450},
    "relay_test": {"passed": true},
    "flow_sensor": {"passed": true, "expected_l": 1.00, "measured_l": 0.99},
    "wifi_mqtt": {"passed": true, "connect_time_s": 6.2},
    "rtc": {"passed": true, "rtc_time": "..."}
  },
  "overall_passed": true
}
```

### 8.5 Storage & export
- Local on-device store (SQLite via `sqflite`, or even a flat JSONL
  file — 10 units/run doesn't need a database).
  - **Persistence pattern**: use the artifact-capabilities `db`
    convention only if this ever becomes a web/artifact tool; for a
    native Flutter app, plain local storage is the right call — no
    cloud dependency needed for a 10-unit run.
- CSV export button reproduces the exact schema `results_logger.py`
  already writes (`timestamp_utc, device_id, tier, passed, steps_json`)
  so historical data from the old Python tool and the new app land in
  one compatible format if you ever need to merge them.
- Share sheet (email/WhatsApp/Drive) for the CSV — no backend required
  for this volume.

## 9. Dependencies on firmware — flag to whoever owns `Config.h`

### 9.1 `SOFTAP_PASSWORD` must stay fixed (or jig must learn the scheme)
[`Config.h`](../../firmware/include/Config.h) already has:
```
#define SOFTAP_PASSWORD  "water1234"  // TODO: derive per-device password for production
```
The jig's auto-join (§6.2) and the app's pre-filled WiFi prompt (§8.2
step 3) both depend on this being **knowable in advance**, not derived
per-unit from something the jig/app can't see. Two options if that TODO
ever gets implemented:
- Keep a **separate fixed password for factory/test builds** (a
  `#ifdef FACTORY_TEST_BUILD` override), only ever flashed on the bench,
  with the real per-device derivation shipped in the release build.
- Or make the derivation a public function of something visible in the
  SSID itself (e.g. last 4 of MAC, which is already typically embedded
  in `DEVICE_ID`) — both jig and app can then compute it without a
  shared secret.
Needs a decision before this tool is built; §6.2/§8.2 assume the
current fixed value.

### 9.2 SSID prefix confirmation
§6.2 assumes a stable, predictable SoftAP SSID prefix. Confirm the
exact current scheme in `computeDeviceId()` (main.cpp) matches what's
documented here before hardcoding it into the jig's scan filter.

## 10. Bill of materials — the tool itself

| Item | Qty | Notes |
|---|---|---|
| Wemos D1 Mini (ESP8266) | 1 | Jig controller |
| 220Ω resistor + LED | 1 | Relay indicator load |
| 10kΩ resistor | 2 | Voltage divider (relay sense) |
| 2-pin JST-XH pigtail | 1 | Flow sensor input, per-unit quick-connect |
| Screw terminal block | 1 | Relay output, per-unit quick-connect |
| Small enclosure/perfboard | 1 | Houses jig + divider, permanent fixture |
| Bench 12V PSU (or USB power bank + boost) | 1 | Relay load supply |
| Dedicated test WiFi AP (old phone hotspot / travel router) | 1 | Bench LAN — likely already owned |
| Optional: MOSFET + driver for DUT USB power-cycle | 1 | Only needed if automating the outage-recovery scenario (Tier 1, not PT) |

Everything else (laptop, phone) is equipment you already have.

## 11. Build plan (once this spec is confirmed)

1. Port `jig_controller.ino` from serial to ESP8266 WiFi HTTP server +
   mDNS + auto-join scan (§6).
2. Wrap `flasher.py`/`serial_monitor.py` in a Flask/FastAPI shim (§7).
3. Scaffold the Flutter production-tester app (§8), starting with
   screens 1–4 (flash + boot + join + core test checklist) since those
   deliver a usable tool even before report/history polish lands.
4. Bench-validate against one real assembled unit end-to-end.
5. Add report/export (screens 5–6).
6. Hand off to Avinash; iterate on anything the first real run surfaces.

## 12. Out of scope for v1

- Environmental/burn-in testing, NIST-traceable flow calibration.
- Automated power-outage-recovery testing (Tier 1 item, needs the
  optional jig MOSFET — not needed for the PT tier this tool targets).
- QR-code device labels / auto-fill of WiFi credentials via QR scan
  (nice ergonomic upgrade once volume grows past hand-typing SSIDs
  being a nuisance).
- Cloud-hosted report storage/dashboard — CSV export is enough at
  10-unit scale; revisit if production volume grows materially.
- iOS support for the production tester app (Android-only is fine for
  an internal bench tool; the consumer app remains cross-platform).
