# FG1 Test Automation

Full spec: [`docs/testing/TEST_JIG_SPEC.md`](../docs/testing/TEST_JIG_SPEC.md)
(test content) and [`docs/testing/PRODUCTION_TOOL_SPEC.md`](../docs/testing/PRODUCTION_TOOL_SPEC.md)
(the phone-operated production tool this repo now also implements).

## Flash Bridge (laptop service the phone app talks to)
```bash
pip install -r requirements.txt
python flash_bridge.py
# Flash Bridge listening on http://0.0.0.0:8787
```
Open `http://<this-laptop-LAN-IP>:8787/` in a browser for a built-in
dashboard (flash + live log, boot log capture, results table) --
useful to sanity-check the bridge itself before the phone app is in
the loop. See `../tools/fg1_production_tester/README.md` for the app.

## What works today (no jig needed)
```bash
pip install -r requirements.txt
python flasher.py esp32dev                       # flash only
python serial_monitor.py /dev/cu.usbserial-0001   # boot log check only
```

## Full production test (needs the physical jig for relay/flow steps
## -- runs fine without it too, those two steps just go unverified)
```bash
python test_production.py --port /dev/cu.usbserial-0001 --jig-host fg1jig.local
```

## Full firmware test suite (per release / board revision)
```bash
python test_full.py --port /dev/cu.usbserial-0001 --jig-host fg1jig.local
```

## Before running against real hardware
1. Update `TEST_AP_SSID`/`TEST_AP_PASSWORD` in `test_production.py` and
   `test_full.py` to match your actual test-station AP (spec section 2.5).
2. Flash `jig_firmware/esp8266_wifi` onto the jig's own MCU (ESP8266)
   once the physical jig (TEST_JIG_SPEC.md section 2) is built --
   `cd jig_firmware/esp8266_wifi && pio run -t upload`. It auto-joins
   whatever DUT SoftAP is currently up (PRODUCTION_TOOL_SPEC.md section
   6.2), so this test PC needs to be on that same network too, same as
   `DutClient`. The original USB-serial sketch is archived at
   `jig_firmware/serial_legacy/` for reference.
3. Results log to `test_results.csv` in this folder (gitignored --
   don't commit real test data; add a line for it if not already ignored).

## Known TODOs (called out in the scripts themselves)
- `jig_firmware/esp8266_wifi`'s pulse rate is fixed at compile time; the
  full-suite's rate sweep needs a `/pulse?n=&rate=` parameter added.
- Cloud `set_cycles` round-trip in `test_full.py` step 9 isn't
  automated yet -- needs a `paho-mqtt` publish from the test PC.
- Calibration persistence check (`test_full.py` step 4) is a manual
  power-cycle today; wire the jig's optional power MOSFET (spec
  section 2.6) to automate it.
