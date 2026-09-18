# FG1 Production Tester -- Laptop CLI

Guided command-line tool for flashing + testing FG1 units, replacing the
phone/Android app now that the test jig handles all WiFi/MQTT
communication with the DUT itself. See
`products/FG1-flowguard/docs/testing/JIG_NETWORK_BRIDGE_SPEC.md` for the
full architecture and `PRODUCTION_TOOL_SPEC_V2.md` for the original
phone-based design this supersedes.

This process **never touches WiFi**. It only talks:
- Plain USB-serial to the jig and the DUT (flashing, boot log, and every
  test step -- all routed through the jig once it joins the DUT's SoftAP,
  and later the office WiFi + MQTT broker).
- Plain HTTPS to the NB Agri Flasher backend (login, build download,
  result reporting) -- doesn't matter what network that happens over.

## Setup

Needs Python 3.9+ and the bundled `bootloader.bin`/`partitions.bin` (not
committed -- copy them from a build of `products/FG1-flowguard/firmware`,
same as the Android app's own `assets/README.md` describes):

```bash
cd products/FG1-flowguard/firmware
pio run -e esp32dev_ds1307
cp .pio/build/esp32dev_ds1307/{bootloader,partitions}.bin \
   ../../projects/tools/production-tester-cli/assets/
```

Then:

```bash
cd projects/tools/production-tester-cli
pip install -r requirements.txt
python run.py --settings   # first run: office WiFi, operator name, station name
```

On later runs, just `python run.py` -- settings and login session persist
in `~/.nbagri_production_tester/`.

### Windows (Avinash's bench)

Same steps, using `py` instead of `python3` if that's how Python's
installed:

```powershell
py -m pip install -r requirements.txt
py run.py --settings
```

Windows COM ports (`COM3`, `COM4`, ...) work the same way as macOS's
`/dev/cu.usbserial-*` -- `pyserial`'s port enumeration handles this
automatically, nothing to configure per-machine.

## What it does, per unit

1. Finds the jig and DUT on USB (PING every port, whichever replies PONG
   is the jig).
2. Reads the DUT's MAC via `esptool`, downloads the selected build,
   erases the whole chip, flashes bootloader+partitions+app.
3. Captures and checks the boot log.
4. Tells the jig to join the DUT's own SoftAP -- from here on, every
   SoftAP-phase command (factory reset, calibration, relay test, flow
   sensor, RTC sync, and finally the office WiFi handover) goes through
   the jig's HTTP bridge to the DUT's local API, not this laptop.
5. Once the DUT joins the office WiFi, the jig joins it too and bridges
   MQTT -- relay/flow/RTC get re-verified, and the final ship-clean reset
   is sent, all through the jig.
6. The jig rejoins the DUT's (now blank) SoftAP one more time to confirm
   the reset actually took.
7. Saves a report (`~/.nbagri_production_tester/reports/`) and reports
   the result to the backend.

## Files

```
production_tester/
├── api_client.py          # NB Agri Flasher backend (login/builds/download/report)
├── jig_client.py          # jig serial protocol -- see JIG_NETWORK_BRIDGE_SPEC.md §4
├── dut_serial.py          # passive DUT serial capture (boot log, WiFi/MQTT wait, reset confirm)
├── flasher.py             # esptool-based flashing (erase, write, read MAC)
├── hardware_discovery.py  # find jig vs DUT on USB
├── boot_log_parser.py     # required-marker / benign-error checks
├── report.py              # TestReport model, JSON + CSV export
├── config.py               # bench settings (office WiFi, operator, station)
└── cli.py                  # the guided flow itself
assets/
├── bootloader.bin          # gitignored, see Setup above
└── partitions.bin
```
