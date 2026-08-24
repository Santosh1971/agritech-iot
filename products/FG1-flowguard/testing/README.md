# FG1 Test Automation

Full spec: [`docs/testing/TEST_JIG_SPEC.md`](../docs/testing/TEST_JIG_SPEC.md)

## What works today (no jig needed)
```bash
pip install -r requirements.txt
python flasher.py esp32dev                       # flash only
python serial_monitor.py /dev/cu.usbserial-0001   # boot log check only
```

## Full production test (needs the physical jig for relay/flow steps
## -- runs fine without it too, those two steps just go unverified)
```bash
python test_production.py --port /dev/cu.usbserial-0001 --jig-port /dev/cu.usbmodem1101
```

## Full firmware test suite (per release / board revision)
```bash
python test_full.py --port /dev/cu.usbserial-0001 --jig-port /dev/cu.usbmodem1101
```

## Before running against real hardware
1. Update `TEST_AP_SSID`/`TEST_AP_PASSWORD` in `test_production.py` and
   `test_full.py` to match your actual test-station AP (spec section 2.5).
2. Flash `jig_firmware/jig_controller.ino` onto the jig's own MCU once
   the physical jig (spec section 2) is built.
3. Results log to `test_results.csv` in this folder (gitignored --
   don't commit real test data; add a line for it if not already ignored).

## Known TODOs (called out in the scripts themselves)
- `jig_controller.ino`'s pulse rate is fixed at compile time; the
  full-suite's rate sweep needs `PULSE:<n>:<rate>` support added.
- Cloud `set_cycles` round-trip in `test_full.py` step 9 isn't
  automated yet -- needs a `paho-mqtt` publish from the test PC.
- Calibration persistence check (`test_full.py` step 4) is a manual
  power-cycle today; wire the jig's optional power MOSFET (spec
  section 2.6) to automate it.
