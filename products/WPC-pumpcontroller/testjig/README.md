# WPC Factory Test Jig

Tests a **WPC Master** or a **Pump Node** end to end. The jig is a **WM1 board** (same SX1262 LoRa wiring as the WPC nodes, plus 6 relays) running `firmware/`; a PC runs `host/wpc_test.py` and talks to **both the jig and the device under test (DUT) through a USB serial hub**.

```
   PC (wpc_test.py) ──USB hub──┬── jig  (WM1, testjig/firmware)
                               └── DUT  (Master or Pump, normal firmware + serial console)
   jig relays / analog out / sense  ──wired to──  DUT inputs / outputs
   jig LoRa  ◄────── radio ──────►  DUT LoRa
```

The DUT runs its **normal production firmware** (v0.4.0+); the only test-specific parts are its serial console and `TESTMODE`. The jig plays the *other side* of whatever is being tested.

## Production Station (browser UI)

```bash
cd testjig/host
pip install -r requirements.txt
python wpc_station.py            # open http://localhost:8788/
```

Modelled on the FG1 Flash Bridge. One page:

1. **Units** -- *Scan USB ports* resets every board and identifies it (Jig / Master / Pump / blank). You choose a role per port, so any number of Masters and Pumps -- including 0 of either -- can be used in one run.
2. **Production mode** -- optional *Flash*, then the functional test (`wpc_test.py`) per unit; each unit ends in `FACTORYRESET`. While one unit is tested every other unit is **held in reset** (a still-joined Pump would otherwise answer a Master under test). Before each test the page asks you to connect that unit to the fixture harness (switch off in Settings if the fixture has both positions permanently wired).
3. **Customer pairing mode** -- 1 Master + n Pumps for a customer: every Pump is pointed at the Master, the page waits until each is joined and online, and the pairing (customer, order, Master ID, Pump IDs/MACs) is appended to `wpc_pairings.csv`. **No factory reset**: units ship as paired. Optionally clears the Master's old pump table first (default on). No Jig needed.
4. **Settings** -- antenna limits (below), fixture flags (`skip IN4`, `skip ADC`), PlatformIO envs, flash attempts, pairing timeout. Saved to `host/wpc_station_settings.json`.

**Antenna test:** the jig averages the RSSI of `Antenna samples` packets from the DUT and compares with a limit set separately for Master and Pump (default -90 dBm). Untick *Enforce* to record the value without failing on it. The measured avg/min/max is in the step detail and in `wpc_test_results.csv`, so you can see the margin before you relax a limit.

CLI equivalents: `wpc_test.py ... --min-rssi -95 --rssi-samples 5 [--rssi-record-only]`.

## What is tested

**Master DUT** — the jig emulates a Pump Node:
1. Float inputs IN1–IN4 read correctly, each isolated (relays RL1–RL4 close a contact to GND).
2. A LoRa `JOIN_REQUEST` is accepted and answered (`JOIN_ACCEPT`), and the pump lands in the Master's table.
3. **Level logic → command correctness**: for levels 1–3, an assigned level open ⇒ the jig receives `LEVEL_CMD` ON; closed ⇒ OFF; closing a level the pump is *not* assigned to changes nothing.
4. Manual override ON/OFF beats level logic; AUTO restores it.
5. IN1/IN4 ADC values carried in `CMD_ACK` reach the Master.
6. Master TX signal strength (RSSI) seen at the jig.
7. Offline detection: pump marked offline when ACKs stop, online again when they resume.
8. Pump table + level assignment survive a reboot (NVS).
9. *Optional* (`--wifi-test`): the jig joins the Master's SoftAP and `GET /status` returns the right Master ID.
10. *Optional* (`--office-ssid`): Master joins office WiFi, publishes retained status to the MQTT broker, and an override command sent from the PC over MQTT reaches the pump over LoRa.

**Pump DUT** — the jig emulates the Master:
1. LEDs (operator confirms with `--visual`).
2. Relay closes/opens its dry contact (the jig senses the contact).
3. IN1 and IN4 analog inputs each track the jig's analog output and are isolated from each other.
4. LoRa: Pump sends `JOIN_REQUEST`, the jig accepts, Pump accepts.
5. `LEVEL_CMD` ON/OFF: Pump ACKs, relay follows (contact sensed), RSSI, and the ADC values inside `CMD_ACK` match the Pump's own reading.
6. Fail-safe: relay opens by itself when commands stop (`TESTMODE` shortens the 60 s timeout to 8 s).

Every run ends with `PASS`/`FAIL` and is appended to a CSV keyed by the DUT's MAC and firmware version. A crash mid-test is recorded as a FAIL, never a silent pass. Both DUTs are left clean at the end (`FACTORYRESET`: default Master ID, empty tables) so they can ship.

## Fixture wiring (WM1 board pins — edit the `#define`s in `firmware/src/main.cpp` if yours differ)

| Jig signal | WM1 pin | Connects to |
|---|---|---|
| Relay RL1 | 74HC595 QA | Master DUT **IN1** (contact between input and GND) |
| Relay RL2 | QB | Master DUT **IN2** |
| Relay RL3 | QC | Master DUT **IN3** |
| Relay RL4 | QD | Master DUT **IN4** (No Power input) |
| `SENSE` | GPIO14 (WM1 "IN1" / No Power input, pull-up) | Pump DUT relay **dry contact** (one side to GND) |
| `AOUT1` | GPIO32 (PWM, 20 kHz, 8-bit) | RC filter → Pump DUT **IN1** |
| `AOUT2` | GPIO33 (PWM) | RC filter → Pump DUT **IN4** |
| LoRa | SPI + NSS5/RST25/DIO1-26/BUSY27 | (radio — antennas, no wire) |

Notes:
- **The WM1 has no DAC** (GPIO25/26, the only DAC pins, are used by LoRa), so the analog stimulus is PWM through an RC filter. The Pump's IN1/IN4 inputs have a 470 Ω series resistor and a **10 kΩ pull-up to 3.3 V**, so even with 0 V driven the pin sits at roughly 0.4 V, and the ADC is set to `ADC_0db` (~1.1 V full scale) so it saturates above that. The default sweep (`--adc-steps 0,15,30,45`, `--adc-min-span 150`) is only a starting point — **tune it on the real fixture**; the pass criterion is "rises monotonically and by at least N mV, and the other channel doesn't move".
- The Master's float inputs are active-low with pull-ups, so a relay contact to GND = "water present at that level".
- Pump ON/OFF logic: a pump assigned to a level runs while that level's switch is **open** (water below the level).

## Setup

```bash
# 1. flash the jig (WM1 board)
cd testjig/firmware && pio run -t upload --upload-port /dev/cu.usbserial-<JIG>

# 2. flash the DUT with the normal firmware (v0.4.0+), e.g. a Master:
cd firmware/master_node && pio run -t upload --upload-port /dev/cu.usbserial-<DUT>

# 3. host script deps
pip install pyserial            # + paho-mqtt only for the optional cloud check
```

## Running

```bash
cd testjig/host
python wpc_test.py master --jig /dev/cu.usbserial-<JIG> --dut /dev/cu.usbserial-<DUT>
python wpc_test.py pump   --jig /dev/cu.usbserial-<JIG> --dut /dev/cu.usbserial-<DUT> --visual

# partial fixture: skip what is not wired yet
python wpc_test.py master ... --skip-in4      # Master IN4 (No Power) not wired
python wpc_test.py pump   ... --skip-adc      # Pump IN1/IN4 analog stimulus not wired

# extras
python wpc_test.py master ... --wifi-test
python wpc_test.py master ... --office-ssid MyOffice --office-pass secret     # cloud round-trip
python wpc_test.py pump   ... --min-rssi -80 --adc-steps 0,10,20,30
```

**Opening a serial port resets these boards on macOS** (even with DTR/RTS held low), so each run starts from a fresh boot and the script waits for the boards to answer `ID` first. The console link is not perfectly clean: on a real Master+Pump pair about 1 query in 100 lost the start of its reply line (occasionally after a burst of garbage bytes), so the script reassembles split lines, finds the `@` tag even after garbage, and retries a lost reply once (never for `REBOOT`/`FACTORYRESET`). `--csv` sets the results file (default `wpc_test_results.csv`).

### No hardware? Use the simulator

```bash
python wpc_test.py master --simulate
python wpc_test.py pump   --simulate --sim-fault norelay       # prove a defect is caught
```

`host/sim_devices.py` models the two consoles well enough to exercise the script's logic and its failure detection (faults: master `noinput nolevel nojoin lowrssi noadc`; pump `norelay crosstalk nofailsafe lowrssi nojoin`). It is a stand-in for the *protocol*, not for the firmware, so a green simulator run says nothing about a real board.

## Serial protocol

One command per line at 115200 baud; replies start with `@`: `@OK key=value...`, `@ERR reason`, `@DATA payload` (always sent *before* the terminating `@OK`/`@ERR`), `@EVT` (asynchronous, from the jig's emulators). Anything else printed is the firmware's normal `[TAG]` log and is ignored.

**Jig:** `ID RELAY <1-6> <0|1>` · `RELAYS <mask>` · `SENSE` · `AOUT <1|2> <0-255>` · `TXPOWER <dBm>` · `PUMPEMU START <masterId> [pumpId]|STOP|ADC <in1> <in4>|NOACK <0|1>|STATUS` · `MASTEREMU START <masterId>|STOP|STATUS|CMD <0|1> [attempts]` · `WIFISCAN <ssid>` · `WIFICONNECT <ssid> [pass]` · `WIFIDISCONNECT` · `HTTPGET <path>` · `HTTPPOST <path> <json>` · `RADIO` · `RESET` · `REBOOT`

**Master DUT:** `ID` · `STATE` · `INPUTS` · `PUMPS` · `ASSIGN <slot> <mask 0-7>` · `OVERRIDE <slot> <auto|on|off>` · `FORGETALL` · `TESTMODE <0|1>` · `TXPOWER` · `WIFI <ssid> [pass]|WIFI "ssid with spaces" [pass]|CLEAR` · `WIFISCAN [START]` · `WIFISTAT` · `LEDTEST [ms]` · `FACTORYRESET` · `REBOOT`

**Pump DUT:** `ID` · `STATE` · `ADC` · `RELAY <0|1>` · `MASTER <hex8>` · `TXPOWER` · `TESTMODE <0|1>` · `LEDTEST [ms]` · `FACTORYRESET` · `REBOOT`

## Status

- Built and compiled; the host script's logic and fault detection are verified against the simulator.
- **Verified on real hardware (Master + Pump, no jig):** both consoles, join, override → Pump relay (4–5.5 s normal mode, 1.4 s `TESTMODE`), the Pump fail-safe (relay dropped after 8.4 s in `TESTMODE`, then rejoined by itself), ADC readout, direct relay drive, and 530 rapid console queries over 4 minutes with no failures after the host-side hardening described above.
- **Not yet run on real hardware:** the jig firmware on a WM1 board, the relay/PWM/sense fixture wiring, and the radio emulation against a real DUT. Expect to tune the RSSI limit and ADC sweep on the first real fixture. The 74HC595 driver copies the WM1 firmware's proven shift order but hasn't been exercised in this project.
- Not covered: LED colours/brightness (visual only), antenna quality beyond RSSI, and the Master's 8 V power stage.
