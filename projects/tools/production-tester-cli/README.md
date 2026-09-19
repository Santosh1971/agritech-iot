# FG1 Production Tester -- Laptop CLI + Web UI

Guided tool for flashing + testing FG1 units, replacing the phone/Android
app now that the test jig handles all WiFi/MQTT communication with the
DUT itself. See `products/FG1-flowguard/docs/testing/JIG_NETWORK_BRIDGE_SPEC.md`
for the full architecture and `PRODUCTION_TOOL_SPEC_V2.md` for the original
phone-based design this supersedes.

This process **never touches WiFi**. It only talks:
- Plain USB-serial to the jig and the DUT (flashing, boot log, and every
  test step -- all routed through the jig once it joins the DUT's SoftAP,
  and later the office WiFi + MQTT broker).
- Plain HTTPS to the NB Agri Flasher backend (login, build download,
  result reporting) -- doesn't matter what network that happens over.

Two ways to drive it: a **browser-based Web UI** (`web_ui.py`) with a
live step checklist and live DUT/jig serial panes -- this is the
recommended one for day-to-day bench use -- and a plain terminal
**CLI** (`run.py`) with the same underlying test logic, for scripting or
a no-browser environment. Both use the exact same `production_tester/`
package underneath, so results and behavior are identical either way.

## Wiring -- jig to DUT

The jig ("Tester PCB", any ESP32 devkit) is a small permanent bench
fixture -- **not** part of the DUT, and it's reused unit after unit.
Both the jig and the DUT connect to the laptop by USB (a hub is fine);
the laptop never talks WiFi itself, only USB-serial to both boards.

| Jig pin | Wire to | Purpose |
|---|---|---|
| **GPIO23** (`PULSE_OUT_PIN`) | DUT's **GPIO35** (`FLOW_SENSOR_PIN`) | Jig injects a known pulse train into the DUT's flow-sensor input, so the flow test checks against a *known* signal instead of trusting the sensor alone. |
| **GPIO35** (`RELAY_SENSE_PIN`) | The relay's own **dry contact** (COM/NO side) | Active LOW when the relay is physically ON -- this is what actually proves the relay switched, not just that firmware wrote the GPIO. GPIO35 is one of the ESP32's input-only pins (34-39), so it has **no internal pull-up** -- the Tester PCB must have its own external pull-up resistor populated on this line, or it floats when idle and gives false readings. |
| **GND** | DUT **GND** | A common ground jumper is required between the two boards regardless of USB power -- without it the pulse signal won't register at the DUT at all, even though the wiring looks right. Don't rely on the USB hub's shared ground alone; run an explicit jumper. |

That's the whole jig-to-DUT wiring -- everything else (WiFi, MQTT,
office router) is wireless and needs no cabling. If a relay-sense or
flow-sensor test ever behaves strangely on a *new* Tester PCB build,
check these three connections (and the pull-up resistor) first before
suspecting software. Full detail and the reasoning behind each pin is
in the jig firmware's own header comment:
`products/FG1-flowguard/testing/jig_firmware/esp32_usb_serial/src/main.cpp`.

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

Then install dependencies once:

```bash
cd projects/tools/production-tester-cli
pip install -r requirements.txt
```

### Windows (Avinash's bench)

Same steps, using `py` instead of `python3` if that's how Python's
installed:

```powershell
py -m pip install -r requirements.txt
```

Windows COM ports (`COM3`, `COM4`, ...) work the same way as macOS's
`/dev/cu.usbserial-*` -- `pyserial`'s port enumeration handles this
automatically, nothing to configure per-machine.

## Running it -- Web UI (recommended for the bench)

```bash
python3 web_ui.py     # Windows: py web_ui.py
```

Then open **http://localhost:8420** in any browser on the same
machine. First time through: sign in (email + one-time code), fill in
the bench settings (office WiFi SSID/password, your name, station
name) and click **Save settings**, then pick the product/build and
click **Use this build**.

To test a unit: plug in the jig and DUT over USB (wiring above), click
**Plug in the unit, then Start Test**. The page shows:
- A live step-by-step checklist (green check / red cross per step, with
  the failure reason if any).
- Two live serial panes -- **DUT serial** and **Jig serial** -- so you
  can watch exactly what's happening on both boards as the test runs,
  not just the final pass/fail.
- A **Stop test** button if you need to abort a run partway through
  (e.g. wrong unit plugged in) -- the run stops cleanly at the next safe
  point instead of leaving things half-configured.

When it finishes you'll see **PASS** or **FAIL** with every step's
detail, and a **Recent results** table below (with a CSV export
button) showing every unit tested this session and before. Click **Test
another unit**, swap the DUT, and go again.

Settings and login persist in `~/.nbagri_production_tester/` between
runs of the tool -- no need to re-enter them each time you restart
`web_ui.py`.

### Terminal CLI (alternative)

Same test logic, no browser, guided by on-screen prompts instead:

```bash
python run.py --settings   # first run: office WiFi, operator name, station name
# later runs: just `python run.py` -- settings/login persist
```

## What it does, per unit

1. Finds the jig and DUT on USB (PING every port, whichever replies PONG
   is the jig).
2. Reads the DUT's MAC via `esptool`, downloads the selected build,
   erases the whole chip, flashes bootloader+partitions+app.
3. Captures and checks the boot log.
4. Joins the DUT's own SoftAP -- from here on, every SoftAP-phase
   command (factory reset, calibration, relay test, flow sensor, RTC
   sync, and finally the office WiFi handover) goes through the jig's
   HTTP bridge to the DUT's local API, not this laptop. If the SoftAP
   can't be joined (e.g. a unit that's already provisioned and sitting
   on the office WiFi), it automatically checks for the DUT over MQTT
   instead, runs the MQTT-phase checks against it as found, then forces
   it back into SoftAP for the rest -- rather than failing the whole
   unit outright.
5. Once the DUT is on the office WiFi, the jig joins it too and bridges
   MQTT -- relay/flow/RTC get re-verified over that path, and the final
   ship-clean reset is sent, all through the jig.
6. The jig rejoins the DUT's (now blank) SoftAP one more time to confirm
   the reset actually took.
7. Saves a report (`~/.nbagri_production_tester/reports/`) and reports
   the result to the backend.

## Files

```
production_tester/
├── api_client.py          # NB Agri Flasher backend (login/builds/download/report)
├── jig_client.py          # jig serial protocol -- see JIG_NETWORK_BRIDGE_SPEC.md §4
├── dut_serial.py          # passive DUT serial capture (boot log, WiFi/MQTT wait, reset confirm) + live tail
├── flasher.py             # esptool-based flashing (erase, write, read MAC)
├── hardware_discovery.py  # find jig vs DUT on USB
├── boot_log_parser.py     # required-marker / benign-error checks
├── report.py              # TestReport model, JSON + CSV export
├── config.py               # bench settings (office WiFi, operator, station)
└── cli.py                  # the guided flow itself (shared by both the CLI and the Web UI)
web_ui.py                   # Flask backend for the browser UI
web/index.html               # the browser UI itself (step checklist, live serial panes, history)
run.py                      # terminal CLI entry point
debug_flow_mqtt.py           # engineering-only: tight repeat-loop diagnostic for the MQTT-phase
                             # flow test, with live DUT/jig serial -- not part of normal bench use
assets/
├── bootloader.bin          # gitignored, see Setup above
└── partitions.bin
```
