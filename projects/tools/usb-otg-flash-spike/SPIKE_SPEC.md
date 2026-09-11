# USB-OTG Flash Spike — Specification

Status: Builds clean (Kotlin + CMake/NDK native module), not yet run against hardware — phase 0 of the [NB Agri Flasher plan](https://claude.ai/code/artifact/8f1bca38-c2d4-45b3-9431-5f62633308fe) (Kamta field-update app). App skeleton lives in [`flasher-spike-app/`](flasher-spike-app/) — see [its README](README.md) for what's implemented vs. what still needs a bench FG1 board.

## 1. Purpose

The NB Agri Flasher plan (§4) calls for Kamta's phone to flash an FG1
device over a USB-OTG cable, with no laptop in the loop. That's exactly
the option `PRODUCTION_TOOL_SPEC.md` §3 looked at for the bench tool and
rejected — "not something to gamble a live production line on for a
first version" — because nobody had verified it against real hardware.

Kamta has no laptop fallback, so this spike exists to replace that
untested assumption with a real answer *before* any backend, admin UI,
or app screens get built around it: **can a phone talk to an FG1
board's ROM bootloader over USB-OTG and flash it, reliably, using
off-the-shelf Android libraries?**

## 2. Scope

**In scope:**
- One Android app, single screen, no auth, no networking.
- Flash a bin file bundled in the app's assets (not fetched from a
  server — that's phase 1+) to a bench FG1 board over USB-OTG.
- Prove the two real unknowns (§4) separately, then together.

**Out of scope (deliberately deferred to later phases):**
- Any backend call, signed URL, or `FlasherGrant` check.
- OTP login or any UI polish.
- Wireless/OTA flashing (§5 of the main plan).
- iOS — USB-OTG serial isn't realistically available there.

## 3. Hardware under test

| | |
|---|---|
| Board | FG1 (`Nursary_OneCH` / `Nursary_One_3in1`) |
| MCU | ESP32 (`esp32dev`) |
| UART bridge | CP2102 (confirmed in KiCad schematic) |
| Partition table | `min_spiffs.csv` — irrelevant here; this spike writes to the existing app partition, doesn't repartition |
| Test bin | Any known-good build from [`products/FG1-flowguard/firmware`](../../../products/FG1-flowguard/firmware) — use the `esp32dev_ds1307` bench environment, not a field DS3231 build (see commit `a32d225`, which made `ds1307` the default bench target) |
| Cable | USB-OTG adapter (USB-A/C female to phone's port) + standard USB-A-to-micro/USB cable to the board |
| Phone | Any Android phone with USB Host (OTG) support — confirm target phone(s) before testing, not all budget Android phones ship OTG-enabled |

## 4. The two unknowns, and how to resolve each

### 4a. Does Android's USB Host API reliably talk to the CP2102 at ROM-bootloader timing?

Flashing needs the ROM bootloader's auto-reset sequence — toggling
DTR/RTS to pull `EN`/`IO0` low at the right moments — which is timing-
sensitive in a way that ordinary serial-console use isn't.

- **Library:** [`mik3y/usb-serial-for-android`](https://github.com/mik3y/usb-serial-for-android) — has a CP210x driver, is the standard choice for USB-serial on Android, actively maintained.
- **Test:** open a connection, exercise DTR/RTS toggling, and confirm the device actually drops into download mode (bootloader prints/responds differently than the app firmware — check via a normal serial read after reset).

### 4b. Can the esptool sync/stub/flash protocol run correctly from Kotlin?

No maintained Android/Kotlin port of esptool's protocol exists (checked
2026-09-11 — nothing beyond esptool.py itself, the JS/WebSerial port
`esptool-js`, and Espressif's own `esp-serial-flasher`). Two real paths,
not a from-scratch protocol implementation:

- **Path A — `esp-serial-flasher` via JNI (recommended starting point).** [`espressif/esp-serial-flasher`](https://github.com/espressif/esp-serial-flasher) is Espressif's own C library, built specifically for flashing their chips from a non-PC host over serial. Cross-compile it for Android (NDK) and call it through a thin JNI bridge, feeding it bytes via the `usb-serial-for-android` connection from §4a. Official, maintained, protocol-correct by construction — the spike's job is proving the Android build + JNI plumbing + real-hardware timing, not re-deriving the protocol.
- **Path B (fallback only, don't start here) — hand-port the protocol.** SLIP framing, `sync` (0x08), `mem_begin`/`mem_data`/`mem_end` stub upload, `flash_begin`/`flash_data`/`flash_end`, optional MD5 check — documented in [esptool's source](https://github.com/espressif/esptool). Only fall back to this if Path A's JNI build proves impractical; it trades a known-correct implementation for full control, which is the wrong trade unless Path A is a dead end.

Do **not** revisit the browser/WebSerial route (`esptool-js` in a WebView) — `PRODUCTION_TOOL_SPEC.md` already flagged that as the higher-risk option for exactly this Android-USB-timing reason, and this app is native anyway.

## 5. Test plan

1. **Bench setup:** one FG1 board, one Android test phone, one bin file, this spike app installed.
2. **Single flash:** flash the bin once, confirm the board boots and runs it (check serial log or an observable behavior — e.g. relay click, WiFi AP name).
3. **Repeatability:** flash 20 times back-to-back without touching the cable. Record success/failure per attempt — this is the number that answers "reliable enough for a dealer to use unsupervised in the field," not a single lucky pass.
4. **Cable/adapter variation:** repeat a smaller run (5x) with a second USB-OTG adapter and cable, since adapter quality is a real field variable Kamta doesn't control.
5. **Failure handling:** deliberately unplug mid-flash once; confirm the app detects the failure cleanly rather than hanging, and that the board is still recoverable (re-flashable) afterward, not bricked.

## 6. Go / no-go criteria

- **Go:** ≥18/20 consecutive flashes succeed on the primary adapter, the board is always recoverable after an interrupted flash, and the JNI build for `esp-serial-flasher` is maintainable (doesn't require patching the upstream library). → proceed to plan phases 1–4 with USB-OTG as the primary transport.
- **No-go / rethink:** reliability is materially below that, or the JNI integration is fragile. → before abandoning the field-USB approach entirely, consider a middle option: a small dedicated USB-serial-to-WiFi bridge dongle Kamta carries (removes the phone-OTG variable entirely, phone talks to it over WiFi/BLE instead) — re-scope with Santosh before committing to that fallback, since it changes the hardware story, not just the app.

## 7. Deliverable

A short written result (pass/fail against §6, actual numbers from §5) plus the spike app's code, kept in this folder. This feeds directly back into the [NB Agri Flasher plan](https://claude.ai/code/artifact/8f1bca38-c2d4-45b3-9431-5f62633308fe) phase 1 kickoff decision.
