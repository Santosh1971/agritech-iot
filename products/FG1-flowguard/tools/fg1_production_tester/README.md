# FG1 Production Tester

A separate small Flutter app (Android only) for the bench operator --
flash, functional-test, and report each assembled FG1 unit. Full design
in [`docs/testing/PRODUCTION_TOOL_SPEC.md`](../../docs/testing/PRODUCTION_TOOL_SPEC.md).
Deliberately not part of the consumer app (`mobile-app/flutter_app`) --
see that spec's section 8.1 for why.

## What this needs to be running/present before you start
1. **Flash Bridge** on the bench laptop -- see `../../testing/README.md`.
   Note the laptop's LAN IP + port (default `8787`) shown when it starts.
2. **Test jig** (ESP8266) flashed with `../../testing/jig_firmware/esp8266_wifi`
   -- powered on, wired to the DUT per `docs/testing/TEST_JIG_SPEC.md`
   section 2.4. It joins the DUT's own SoftAP automatically once one
   appears (see PRODUCTION_TOOL_SPEC.md section 6.2) -- nothing to
   configure on it per-unit.
3. **Bench LAN** -- the phone and laptop both need to be on it before
   starting a flash. The DUT does not need to be on it until step 7 of
   the test sequence (WiFi/MQTT check) -- see section 5 of the spec.

## Build & run
```bash
cd tools/fg1_production_tester
flutter pub get
flutter run   # with an Android device/emulator attached
# or, to build an installable APK:
flutter build apk --debug
# -> build/app/outputs/flutter-apk/app-debug.apk
```

## First-time setup on the phone
Open the app -> tap the gear icon (Settings) -> fill in:
- **Flash Bridge host:port** -- e.g. `192.168.1.50:8787` (the laptop's
  LAN IP, from Flash Bridge's startup message)
- **Jig host** -- leave as `fg1jig.local` unless mDNS doesn't resolve
  on your phone, in which case use the jig's IP from its serial log
  (`pio device monitor` while it's booting) or its `/status` endpoint
  once you know any IP to reach it at
- **Bench LAN (Test AP) SSID/password** -- whatever WiFi network you
  want each unit's STA side to join for the WiFi+MQTT check
- **Expected pulses/liter**, **flow test pulse count** -- defaults
  (450/450) match `testing/test_production.py`'s constants
- **Operator name**, **station name** -- for the report record

## Per-unit flow
1. Plug the DUT into the laptop via USB.
2. Tap **Flash** (optionally enter the exact serial port if PlatformIO
   can't auto-detect it -- e.g. `/dev/cu.usbserial-0001`).
3. Watch the live flash log; boot log check runs automatically after.
4. App prompts you to **join the device's own WiFi network** (shown as
   the SSID, e.g. `SWC_001_A1B2`) -- open system WiFi settings, join it
   (password is whatever `SOFTAP_PASSWORD` is set to in the firmware's
   `Config.h`, currently a single fixed value for all units), come back,
   tap Continue.
5. Tests run automatically (factory reset, calibrate, relay, flow,
   WiFi+MQTT, RTC, ship-clean reset) -- watch the checklist.
6. Report screen shows PASS/FAIL. Tap **Save & Next Unit**, rejoin the
   Bench LAN, plug in the next unit.
7. **Run History** (clock icon) lists every unit tested, with a CSV
   export button (share sheet -- email/WhatsApp/Drive).

## Known limitations (v1, see spec section 12)
- Android only (no iOS build target).
- No QR-code WiFi credential entry -- SSID/join is manual, same as the
  consumer app's own provisioning flow.
- If two DUTs are powered on the bench at once, the jig may auto-join
  the wrong one (see PRODUCTION_TOOL_SPEC.md section 6.2) -- keep it to
  one unit at a time.
