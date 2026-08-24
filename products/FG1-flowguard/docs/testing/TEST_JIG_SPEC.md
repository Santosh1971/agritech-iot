# FG1 Test Jig & Automated Test Specification

Status: Draft v1 — written before the physical jig exists. The flash +
boot-log automation (Stage 1 / `test_production.py --flash-only`) is real,
runnable code today. Everything that depends on the jig controller
(pulse injection, relay/LED sensing) is written against a defined
protocol (see §4) so it can be wired up and tested against real hardware
without changing the test scripts — only `jig_controller.py`'s transport
needs to talk to real hardware once built.

## 1. Philosophy — two test tiers

| | **Full Firmware Test Suite** | **Production Test (PT)** |
|---|---|---|
| Who runs it | Firmware dev, before a release/board-rev ships | Every single unit, on the line |
| Goal | Catch regressions across the whole feature set | Catch manufacturing defects (bad solder joint, dead relay, DOA WiFi) |
| Target time | Minutes, thoroughness over speed | Under 90 seconds per unit |
| Covers | Everything in §5 | The minimal set in §6 — still touches every subsystem (LED, WiFi, MQTT, relay, flow, RTC) but with one fast check each, not a sweep |

Both tiers share the same underlying building blocks (§3 Python
modules) — PT is a strict subset of the full suite's steps, not a
separate codebase.

## 2. Test jig hardware

### 2.1 Why no pogo-pin bed-of-nails
FG1 uses a standard ESP32-30pin dev kit (see `platformio.ini` —
`board = esp32dev`), which already has USB-to-serial and auto-reset
(DTR/RTS) built in. Flashing is just a USB cable — no programming jig
needed. The jig's job is purely **functional verification** of the
assembled product (dev kit + relay driver + flow sensor input + RTC),
not silicon-level programming.

### 2.2 DUT connections
| Signal | DUT pin | Jig side |
|---|---|---|
| Flash + serial log | USB | Test PC, direct |
| Flow sensor input | GPIO35 (`FLOW_SENSOR_PIN`) | Jig controller pulse output |
| Relay output (switched side) | Relay/ULN2003 output terminal | Jig sense circuit (see 2.4) |
| WiFi | — (RF, no wired connection) | Dedicated test AP (see 2.5) |

Use a 2-pin JST-XH pigtail on the flow sensor input and a screw
terminal on the relay output so a board just clips into the jig — no
soldering per-unit.

### 2.3 Jig controller
A small permanent fixture microcontroller — **Arduino Nano or
ESP8266, ~$3, one-time build** — does two things a test PC can't do
directly:

1. **Emits a precisely-counted pulse train** into the DUT's flow
   sensor pin, so calibration math is checked against a *known* input
   instead of trusting the sensor.
2. **Senses whether the relay physically switched** — a small
   indicator load (LED + resistor, ~20mA) wired to the relay's
   switched output, with a voltage divider feeding the jig
   controller's ADC. This catches a dry solder joint or dead relay
   that firmware's own "\[RELAY\] ON" log line would never reveal
   (the GPIO write succeeds even if nothing downstream is connected).

See `jig_firmware/jig_controller.ino` (§4) for the sketch — it's a thin
serial-command surface (`PULSE:<n>`, `RELAY?`, `LED?`) the Python test
scripts drive directly.

### 2.4 Relay sense circuit
```
DUT relay output ──┬── 220Ω ── LED ── GND
                    └── 10kΩ ── Jig controller ADC pin
                              └── 10kΩ ── GND
```
A simple voltage divider across the indicator LED's forward voltage —
close enough to detect "current is flowing" vs "open circuit" without
needing precision. Swap the LED for a real 12V lamp + a level-shifted
divider if you want a visual pass/fail a human can also glance at.

### 2.5 Dedicated test WiFi AP
Keep one small, always-on AP at the test station (an old phone on
hotspot, or a cheap travel router) with fixed credentials baked into
the test script. Every unit joins this network during PT — this single
step proves antenna soldering, the WiFi module itself, MQTT
credentials, and broker reachability all at once via `device_info`
reporting `wifi_connected` and `mqtt_connected` both true.

**Point this AP's DNS/gateway at your real MQTT broker** (or a
staging broker if you don't want test units cluttering production
topics) so the MQTT check is a real end-to-end proof, not just a LAN
join.

### 2.6 Power
DUT is USB-powered from the test PC (also its serial/flash link).
Feed the relay's switched side from a small bench 12V supply (or a
USB power bank + boost converter) so there's a real load to sense —
doesn't need to be a real pump, the LED indicator load above is
enough.

**Optional but valuable:** put a MOSFET on the DUT's USB 5V line,
controlled by the jig controller, so the test script can
*programmatically power-cycle the DUT mid-test*. This automates the
exact outage-recovery scenario that was manually tested by hand
today (power off mid-cycle, confirm it resumes correctly) — genuinely
useful for the Full suite, not needed for PT.

## 3. Python test harness — modules

All code below is real and runnable except `jig_controller.py`'s
serial transport, which needs the physical jig to actually respond
(the interface and protocol are final; only "does hardware answer"
depends on the jig existing).

```
testing/
├── flasher.py           # wraps `pio run -t upload`, returns success/fail
├── serial_monitor.py     # tails boot log, checks against expected markers
├── dut_client.py         # sends {"cmd": ...} JSON over the DUT's local WS API
├── jig_controller.py     # serial link to the jig's Arduino/ESP8266
├── results_logger.py     # CSV logging: device_id, timestamp, pass/fail per step
├── test_production.py    # Tier 2 orchestration — fast, every unit
├── test_full.py          # Tier 1 orchestration — thorough, per release
└── jig_firmware/
    └── jig_controller.ino
```

## 4. Jig controller protocol (serial, 115200 baud)

| Command (PC → jig) | Response | Meaning |
|---|---|---|
| `PULSE:<n>\n` | `OK:<n>\n` | Emit exactly n pulses on the flow-sim output, then reply |
| `RELAY?\n` | `RELAY:ON\n` or `RELAY:OFF\n` | Current sensed state of the relay indicator circuit |
| `LED:<name>?\n` | `LED:ON\n` or `LED:OFF\n` | Sensed state of a monitored LED (`wifi`, `flow`) — only if photosensors are populated (optional, §2.3) |
| `PING\n` | `PONG\n` | Liveness check before starting a test run |

## 5. Full Firmware Test Suite (Tier 1)

Run before shipping a new firmware version or validating a new board
revision. Order matters — each step assumes the previous ones passed.

1. **Flash** — `pio run -e esp32dev -t upload`, capture exit code
2. **Boot log** — tail serial, require these markers within 10s, zero unexpected `[E]` lines:
   `[BOOT] SmartWaterController starting...`, `[NVS] Initialized OK`, `[RTC] Time:`, `[RELAY] Initialized`, `[FLOW] Initialized`, `[BOOT] Device ID:`
3. **Factory reset** — send `factory_reset`, confirm reboot + clean NVS (boot log shows no saved cycles/WiFi)
4. **Calibration set + persist** — `calibrate {ppl: 450}`, power-cycle (via jig MOSFET if populated, or manual), confirm `device_info` still reports the custom calibration — catches NVS write reliability issues
5. **Relay physical test** — `relay_test`, jig `RELAY?` must read `ON` during the pulse window and `OFF` after
6. **Flow sensor accuracy sweep** — inject pulse trains at multiple rates (slow ~5 pulses/sec, fast ~50 pulses/sec) via jig `PULSE:<n>`, check `device_info.liters_delivered` against `n/450` within 2% at each rate
7. **WiFi STA connect** — `wifi_config` to the test AP, confirm `wifi_connected: true` within 15s
8. **MQTT connect** — confirm `mqtt_connected: true` within 10s of WiFi joining; confirm broker actually receives the retained `status`/`lwt` topics (subscribe from the test PC side too, don't just trust the DUT's self-report)
9. **Cloud cycle round-trip** — publish a `set_cycles` payload to the retained `cycles_config` topic (same mechanism the app uses), confirm the device applies and persists it (`get_cycles` echoes it back)
10. **Full cycle lifecycle** — trigger a short cycle, inject matching flow pulses, confirm `cycle_active` transitions true→false at the right time, confirm a `get_history` entry was created with the correct liters
11. **Power-outage resilience** (if jig MOSFET populated) — cut power mid-cycle, restore, confirm it resumes and completes using accumulated `elapsed_seconds`, not wall-clock
12. **SoftAP fallback** — force WiFi off, confirm SoftAP starts within the expected window, confirm the Local WS API (`get_cycles`, `get_history_range`) responds correctly while in fallback
13. **Local WiFi setup flow** — from the test PC, join the DUT's SoftAP and replay the exact `wifi_scan` → `wifi_config` → `resume_auto_mode` sequence the app uses, confirm it rejoins the test AP
14. **LED visual/photosensor check** — if photosensors populated, verify `wifi` LED and `flow` LED states via jig `LED:<name>?` at the appropriate moments; otherwise a human glances at the board during steps 5–7 (acceptable for Tier 1 since it's low-volume, not a per-unit bottleneck)
15. **Ship-clean reset** — final `factory_reset`

## 6. Production Test (PT / Tier 2)

Fast, every unit, single pass per subsystem — still touches LED, WiFi,
MQTT, relay, flow, and RTC, just without the exhaustive sweep of Tier 1.

1. Flash firmware
2. Boot log check (same markers as Tier 1 step 2)
3. `factory_reset`
4. `calibrate {ppl: 450}` — no power-cycle persistence check, just confirm the ack
5. `relay_test` + jig `RELAY?` confirms physical switch (covers **relay**)
6. Jig `PULSE:450` while watching `device_info.liters_delivered` → expect `1.00 ± 0.02` L (covers **flow sensor**)
7. `wifi_config` to the test AP → poll `device_info` until `wifi_connected` **and** `mqtt_connected` both true, 20s timeout (covers **WiFi** and **MQTT** in one shot)
8. RTC sanity — `device_info.rtc_set` must be `true` and `rtc_time`/`rtc_date` non-empty (covers **RTC**)
9. **LED** — tester does a 2-second visual glance during steps 5 and 7 (WiFi LED should be solid once connected, flow LED should blink during the pulse burst); upgrade to jig photosensor `LED:` queries later if a fully hands-off line is worth the extra jig complexity
10. `factory_reset` — ship clean
11. Log result: device ID (parsed from boot log), timestamp, PASS/FAIL per step, to CSV

Target: **under 90 seconds per unit**, steps 5–8 run essentially back
to back since step 7's WiFi timeout is the long pole.
