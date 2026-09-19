# FG1 Production Flash/Test/Report Tool — Specification v2 (phone-only)

> **Supersedes [`PRODUCTION_TOOL_SPEC.md`](PRODUCTION_TOOL_SPEC.md) entirely.**
> That doc's architecture (Flutter app + bench laptop "Flash Bridge" +
> ESP8266 WiFi jig + hotspot/travel-router test AP) was never built as
> described — what actually exists and runs on the bench today is a
> **native Android app doing everything, no laptop, no hotspot**. This
> doc describes that real system. [`TEST_JIG_SPEC.md`](TEST_JIG_SPEC.md)
> §5/§6 (the Tier 1 / Tier 2 test *content* — what to check, not how) is
> still the procedural reference this tool implements.

Status: Draft v2, written 2026-09-16 against the app and firmware as
they actually exist in this repo (`projects/tools/flasher-tester-app`,
`products/FG1-flowguard/firmware`, `products/FG1-flowguard/testing/jig_firmware/esp32_usb_serial`).
Not yet fully built — §5 is the agreed session flow; §7 is new app-side
work (`MqttCommander`) not yet written.

## 1. Purpose

Avinash is assembling ~10 units of FG1. Each unit needs, on the bench:
flash firmware → verify it boots clean → verify every subsystem (relay,
flow sensor, WiFi, MQTT, RTC) on *this specific assembled board* →
produce a pass/fail record. One operator, one phone, nothing else.

## 2. Roles / hardware

- **Operator** — uses only the phone (OnePlus CPH2585 today, Android
  16). Never touches a terminal or laptop.
- **Phone app** — "Tester" (`com.nbagri.flashertester`, forked from the
  proven NB Agri Flasher). Flashes the DUT over USB-OTG, drives every
  functional check, talks to the jig over USB-serial, talks to the DUT
  over its local WS API and (new, §7) over MQTT, generates the report.
- **Test jig** — a small permanent-fixture ESP32 dev board ("Tester
  PCB"), wired to the **same** USB-OTG hub as the DUT. Pure USB-serial,
  **no WiFi at all**. Emits flow-sensor pulses and senses the relay's
  dry contact. See [`esp32_usb_serial/src/main.cpp`](../../testing/jig_firmware/esp32_usb_serial/src/main.cpp).
- **Office WiFi router** — the site's existing network. This is the
  *only* test AP used. **A phone mobile hotspot is explicitly rejected
  for this role** — see §4.1.

No Flash Bridge, no laptop, no separate WiFi jig, no dedicated bench AP
— all four are dropped from the v1 spec.

## 3. Why USB-OTG hub instead of "laptop flashes, phone tests"

Both the DUT and the jig are plain USB-serial devices. Android's USB
host API supports multiple simultaneous `UsbDeviceConnection`s through
a powered hub — one per physical device — so the phone can hold the
jig's serial link open for the entire session while independently
opening/reading the DUT's serial link, with no conflict. The app
auto-detects which port is which by sending `PING` on every port and
treating whichever one replies `PONG` as the jig (`MainActivity.probeAllAndAssignRoles()`).
Flashing the DUT itself uses Espressif's `esp-serial-flasher` C library
via JNI, the same one already proven in the shipped NB Agri Flasher app.

## 4. Networking model

### 4.1 Why office WiFi, not a phone hotspot
Bench-tested and rejected 2026-09-16: with the phone simultaneously
**hotspot-ON** (to be the DUT's test AP) **and WiFi-client-joined** to
the DUT's own SoftAP (to run WS commands), Android routed the hotspot's
own internet uplink through that dead-end WiFi link instead of
cellular. The DUT associated with the hotspot fine but every DNS lookup
(including the MQTT broker's hostname) failed. Working around it needed
an explicit "turn phone WiFi off, keep hotspot on" operator step before
every MQTT check and "turn it back on" after — fragile, manual, and
specific to the hotspot NAT/uplink quirk.

**None of this applies to a real router.** The office WiFi network has
its own internet backhaul independent of the phone entirely, so the
phone and the DUT can both be plain WiFi *clients* on it — no
AP-mode-on-the-phone involved, so the uplink-routing quirk above simply
doesn't exist. Decision: **office WiFi only, hotspot path dropped.**

### 4.2 What Android will and won't automate
Established 2026-09-16, still true and shapes §5 below:
- Joining a *known* SSID+password can use `WifiNetworkSpecifier`, but
  Android always shows a one-tap system confirmation dialog — never
  fully silent.
- Turning phone WiFi on/off, or mobile data on/off: **no API** for a
  third-party app since Android 10. Best available is deep-linking to
  Settings and asking the operator to tap.
- Starting a real, internet-backed hotspot: **no API** for a
  third-party app at all (moot now per §4.1 anyway).

So every WiFi *network switch* on the phone is an operator action.
§5 is designed to need exactly **two** of them per unit (down from the
four implied by a literal "switch to home WiFi and back" reading of the
original proposal — see §6 for why the middle two aren't needed).

## 5. Session flow (per unit)

| # | Step | Phone's WiFi | Talks to |
|---|---|---|---|
| 1 | Plug DUT + jig into the USB-OTG hub. App probes both ports (`PING`/`PONG`), confirms jig + DUT found. | — (USB only) | Jig, DUT (both USB-serial) |
| 2 | Tap Flash. App reads DUT MAC over USB, downloads the selected build from the backend, flashes bootloader+partitions (bundled) + app.bin (downloaded) over USB-OTG. | — | DUT (USB), backend (HTTP, over whatever network the phone already has — cellular is fine) |
| 3 | Capture boot log over USB serial (10s), parse device ID + firmware version, report result to backend for history. | — | DUT (USB), backend (HTTP) |
| 4 | **Manual step 1/2**: app prompts "Join the DUT's own WiFi network now", opens WiFi settings. Operator taps the DUT's SoftAP SSID. | → DUT SoftAP | — |
| 5 | `factory_reset`, confirm reconnect. | DUT SoftAP | DUT (WS) |
| 6 | "Factory settings": `calibrate {ppl: 450}` — writes and confirms the shipped calibration factor. *(Flagging: confirm this is what "Factory settings" in the original ask meant — see §6.3.)* | DUT SoftAP | DUT (WS) |
| 7 | `relay_test` + jig `RELAY?` — confirms physical relay switch, not just the GPIO write. | DUT SoftAP | DUT (WS) + Jig (USB) |
| 8 | Jig `PULSE:<n>` (n = 450 × target liters) while reading `device_info.liters_delivered` before/after. | DUT SoftAP | DUT (WS) + Jig (USB) |
| 9 | `rtc_sync {unix: <phone's current epoch>}`, then `device_info` to confirm `rtc_set: true` and `rtc_time` matches. | DUT SoftAP | DUT (WS) |
| 10 | `wifi_config {ssid, pass}` = the office router's credentials, then `resume_auto_mode`. | DUT SoftAP (still) | DUT (WS) |
| 11 | **No operator action.** DUT's SoftAP tears itself down automatically ~60s after its STA side is stable (`main.cpp` — "Stable for 60s — leaving local fallback, dropping SoftAP"); the phone's WiFi just idles/drops on its own. Evidence for this step is DUT serial (`[WiFi] Connected`, `[MQTT] Connected`) captured live over USB, plus an independent broker-side check (phone subscribes to `agrisense/FG1/<id>/status` directly, bound to cellular). | (idle / dropping) | DUT (USB serial) + MQTT broker (cellular) |
| 12 | **Re-verify relay, flow, RTC — now over MQTT**, not WS (§6, §7): publish `relay_test`/`calibrate`-check/`rtc_sync`-check as MQTT commands to `agrisense/FG1/<id>/command`, read results from the next `status` publish (≤5s later) and the jig (relay sense, pulse injection — jig stays reachable over USB the whole time regardless of the phone's WiFi state). | (irrelevant — MQTT is over cellular) | DUT (MQTT) + Jig (USB) |
| 13 | `factory_reset` — published as the final MQTT command. Ship-clean. | (irrelevant) | DUT (MQTT) |
| 14 | **Manual step 2/2**: DUT reboots, drops WiFi+MQTT, comes back up in blank SoftAP mode (no saved credentials — confirms the reset actually took). App prompts "Rejoin the DUT's WiFi to confirm factory reset", opens WiFi settings. | → DUT SoftAP | — |
| 15 | Confirm: `device_info` over WS shows no WiFi/MQTT config, **and** the boot log captured over USB serial shows `"starting local fallback (SoftAP)"` with no saved-SSID line — two independent confirmations of the same fact, one via WS, one via serial. | DUT SoftAP | DUT (WS + USB serial) |
| 16 | Report generated (§8), saved locally + reported to backend. Operator unplugs, moves to next unit. | — | backend (HTTP) |

## 6. What changed vs. the literal "switch to home WiFi and back" idea, and why

The original proposal was: after handing WiFi credentials to the DUT,
have the phone *also* switch its own WiFi to the office network and
re-run every check there, then switch back to the DUT's SoftAP at the
end to confirm factory reset.

### 6.1 The blocker
`DutWsClient` is hardcoded to `ws://192.168.4.1/ws` — the DUT's fixed
SoftAP gateway address. Once the DUT joins the office router, it gets a
**DHCP-assigned IP the phone has no way to know**, and — per §5 step 11
— the DUT's SoftAP is torn down automatically ~60s after that, so
`192.168.4.1` stops answering entirely. There's no mDNS in the firmware
and `device_info` doesn't currently report the DUT's STA-side IP
either. A literal "phone joins home WiFi, re-runs WS checks" step
**would not work as described** without either adding mDNS or adding an
IP field to `device_info` and some discovery step — both firmware
changes.

### 6.2 The fix already available, no firmware change needed
`MQTTClient.cpp` subscribes to `agrisense/FG1/<id>/command` and routes
every message straight into the **same `handleCommand()` dispatcher**
the local WS API uses (`main.cpp:645` — "return value unused on the
MQTT path"). So `relay_test`, `calibrate`, `rtc_sync`, `factory_reset`
etc. already work identically over MQTT — the firmware doesn't
distinguish where a command came from. Combined with the periodic
`status` publish (every `STATUS_PUBLISH_INTERVAL_MS` = 5s), the app can
command-and-verify the DUT over MQTT with **zero firmware changes**.

This also means the phone **never needs to leave the DUT's SoftAP
connection at all** during the handover — it can just let that
connection idle/drop on its own (§5 step 11) while running the
post-handover checks over MQTT (bound to cellular, same pattern
`MqttChecker` already uses). That removes two of the four WiFi switches
the literal proposal implied, and — as a side effect — makes the whole
hotspot on/off dance from 2026-09-16 moot, since the phone's WiFi state
during this window stops mattering at all.

### 6.3 Open item to confirm
"Factory settings" in the original message — read here as §5 step 6
(`calibrate {ppl: 450}`, the shipped default). Flag if something
broader was meant (e.g. a distinct "restore all defaults" pass) — that
would collide semantically with the final `factory_reset` in step 13
and needs a different firmware command if it's meant to be separate.

## 7. New app-side work: `MqttCommander`

`MqttChecker.kt` today only *subscribes and waits* (used once, for the
WiFi+MQTT step's broker-side proof). §5 step 12 needs a superset:
**publish** a command JSON to `agrisense/FG1/<id>/command`, then wait
for the next `status` message and read the relevant field back
(`pump_on` during `relay_test`'s pulse window, `liters_delivered`
before/after a jig pulse burst, `rtc_set`/`rtc_time` after `rtc_sync`).
Same connect/bind-to-cellular/disconnect pattern as `MqttChecker`,
extended with a publish call and a slightly longer wait (worst case
~5s for the next periodic status tick, vs. instant for a WS reply).

## 8. Flow-rate capture ceiling — separate diagnostic item, keep in the plan

Not part of the per-unit PT flow, but flagged during this planning pass
and worth building before it's needed: the DUT's flow ISR
(`FlowSensor.cpp`) has no debounce and no documented maximum reliable
pulse rate — the real ceiling is set by ESP32 interrupt-dispatch
overhead and by WiFi/MQTT's own brief interrupt-masking, neither of
which can be determined by reading code alone. Two prerequisites once
someone wants to actually measure it:
- **Jig pulse rate must become settable** (`PULSE:<n>` currently always
  fires at a fixed 1kHz = ~8000 L/hr at 450 ppl, hardcoded in
  `esp32_usb_serial/src/main.cpp`'s `PULSE_HIGH_US`/`PULSE_LOW_US`) —
  needs a new command, e.g. `RATE:<hz>` before `PULSE:<n>`.
  App computes Hz from a target L/hour and the unit's calibration
  factor: `Hz = Lph × ppl / 3600`.
  - **Also useful here**: no firmware change needed for the app to know
    what actual flow rates this product should be tested against —
    just needs real-world target L/hr figures to plug into that
    formula.
- **Sweep procedure**: drive the jig at increasing rates, compare
  `liters_delivered` against the known injected count, once with
  WiFi/MQTT idle and once while actively publishing (that's where a
  WiFi-driver-masking gap would actually show up, not in an idle bench
  test).

## 9. Report data model (per unit)

```json
{
  "device_id": "SWC_001_B468",
  "firmware_version": "1.2.0",
  "build_label": "FG1 1.2.0 (esp32dev_ds1307)",
  "timestamp_utc": "2026-09-16T12:00:00Z",
  "operator": "Avinash",
  "station": "bench-1",
  "steps": {
    "factory_reset":  {"passed": true},
    "calibrate":      {"passed": true, "ppl": 450},
    "relay_test":     {"passed": true, "detail": "jig sensed relay ON"},
    "flow_sensor":    {"passed": true, "detail": "expected 1.00L, got 0.99L"},
    "rtc":             {"passed": true, "detail": "rtc_time=12:00:03"},
    "wifi_mqtt":      {"passed": true, "detail": "confirmed via DUT serial; broker confirms status message received"},
    "relay_test_mqtt": {"passed": true},
    "flow_sensor_mqtt":{"passed": true},
    "rtc_mqtt":        {"passed": true},
    "ship_clean_reset": {"passed": true},
    "factory_reset_confirmed": {"passed": true, "detail": "SoftAP reappeared, no saved WiFi in boot log"}
  },
  "overall_passed": true
}
```

Matches the existing `TestReport.kt` shape; the three `*_mqtt` steps
and the final `factory_reset_confirmed` step are new additions this
spec introduces.

## 10. Firmware command reference (as it exists today, no changes needed for §5–§7)

| Command | Transport(s) | Payload | Effect |
|---|---|---|---|
| `factory_reset` | WS, MQTT | — | `nvs.factoryReset()` (full NVS clear) + reboot |
| `calibrate` | WS, MQTT | `{ppl}` | Sets + persists pulses-per-liter |
| `relay_test` | WS, MQTT | — | `relay.testPulse(5000)` |
| `rtc_sync` | WS, MQTT | `{unix}` | Sets RTC from a Unix timestamp |
| `wifi_config` | WS, MQTT | `{ssid, pass}` | Saves STA credentials, schedules background connect |
| `resume_auto_mode` | WS, MQTT | — | Fast-forwards the WiFi retry timer |
| `device_info` | WS (useful), MQTT (response discarded — §6.2) | — | Full status JSON |

All routed through the single `handleCommand()` in `main.cpp` — this is
why §6.2's MQTT-command approach needs no firmware work.

## 11. Out of scope for v2

- Environmental/burn-in testing, NIST-traceable flow calibration.
- Automated power-outage-recovery testing.
- QR-code device labels / WiFi-credential QR scan.
- Cloud-hosted report dashboard — local + backend `reportResult` is
  enough at this volume.
- iOS — Android-only bench tool.
