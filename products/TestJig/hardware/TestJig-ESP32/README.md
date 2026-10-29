# TestJig-ESP32 — production test jig (rev D, draft for review)

Test jig for our product range (WM1 WaterMaster first). Runs entirely from the ESP32 DevKit's USB. 2 layers, **99 × 99 mm, two M3 holes on the vertical centre line 92 mm apart**, to suit the jig's IP65 box (same boss spacing and hole size as WM1).

| File | What |
|---|---|
| `TestJig.kicad_sch` / `out/TestJig-schematic.pdf` | Schematic (net-label style, one A1 sheet, grouped by block) |
| `TestJig.kicad_pcb` | Placed and auto-routed PCB, GND pour on both layers |
| `out/TestJig-3D-top.png`, `out/top-copper.png`, `out/bottom-copper.png` | Renders for quick review |
| `out/TestJig-pcb-top.pdf`, `out/TestJig-pcb-bottom.pdf` | Copper + silk plots |
| `out/fab/` | Gerbers + drill (`TestJig-gerbers.zip`), BOM, **sourcing BOM with Quartz/Robu links**, top-side pick-and-place |
| `out/routed-good.kicad_pcb` | Backup of the current fully routed board |
| `TestJig.kicad_sym`, `TestJig.pretty/` | Project-local DevKit symbol + footprint |
| `tools/` | Generator scripts (`design.py` is the single source of truth for the netlist) |

## Rev D changes (2026-10-21): fitted to the jig box
- **Board and holes:** 99 × 99 mm square board. Two M3 holes at (49.5, 3.5) and (49.5, 95.5), i.e. the top-centre and bottom-centre bosses, 92 mm apart.
- **Left wall (inputs, with the cable glands):** J1–J4 relay sense (IN 1–16), J9 PULSE IN (I1, I2, G, 3V3), J11 AIN (A1, A2, G).
- **Right wall (outputs):** J5–J8 DUT drive (OUT 1–16), J10 4–20 mA (A1, A2, G), J14 PULSE OUT (O1, O2, G).
- **Pulse connector split:** the old 6-pin J9 is now J9 (in, left) and J14 (out, right).
- **DevKit USB:** exits the right wall through a 12 mm gap between J6 and J7.
- **LoRa:** SMA points straight up through the top wall.
- **Connector silkscreen:** every connector pin has its label beside it (channel number, G, V, I1, O1, A1…), plus an arrow showing the signal direction and the bank's resistor group label.
- **4–20 mA stages:** they sit under the DevKit; their PWM RC filters moved to the strip below it.
- **Straight output and LED tracks:**
  - output 330R (R141–R156, R4/R5): pad 1 (595 side) faces the IC, pad 2 faces the connector pin;
  - LEDs: anode faces its 470R, cathode faces the top edge and GND pour; '+' sits between LED and resistor, the P F B L 1–4 letters above the LEDs.

## Rev C changes (2026-10-21)
- **WM1 board outline and mounting holes:**
  - 4 holes at (4.78, 33.57), (92.79, 33.57), (4.78, 83.58) and (92.79, 83.58) mm from the top-left. WM1's top-centre hole is not used.
  - Full re-placement: LoRa moves to the top-right corner, J9/J12/J11 sit on the right edge between the holes, and the 4–20 mA output (J10) moves to the bottom edge.
- **The two 4–20 mA stages sit under the socketed DevKit.** They are low SMD parts only; the courtyard covers the header rows. They're visible and reworkable with the DevKit unplugged. The tall self-test jumpers stay outside.
- **Silkscreen:**
  - reference and value for every part (ICs too) at 0.8 mm, clear of pads and part outlines;
  - '+' at every LED anode, and '1' at pin 1 of every IC and module.
- **BAT54WS uses the SOD-323 hand-soldering footprint.**
- **Tracks:**
  - signals routed at 0.4 mm and widened to 0.5 mm wherever clearance allows;
  - +5V/+3V3 routed at 0.8 mm and widened to 1.0 mm wherever possible.
- **Pin swap:** SR_IN moves to IO26 and LoRa DIO1 to IO15, because the long SR_IN run couldn't be routed across the populated DevKit area. DIO1 is an input; if it is low at boot, GPIO15 only mutes the boot log.
- **Schematic:** every block is drawn as a wired, left-to-right circuit (connector → resistor → node/pull-up → IC) with notes, like the 4–20 mA block.

## Rev B changes (2026-10-21): hand-soldering + local sourcing
- **Resistor arrays removed.** The 4×0603 arrays (0.8 mm pitch) are replaced by discrete **0805** resistors, using KiCad's hand-solder pads:
  - R101–R116: 1k relay-sense series
  - R121–R136: 10k pull-ups
  - R141–R156: 330R DUT-output series
  - R161–R168: 470R LED
  - R171–R178: 10k jig-input pull-ups
- **All R/C are 0805 hand-solder pads.** C1 changes from 22 µF 1206 to 10 µF 0805 (in stock at Quartz).
- **LEDs are now 1206** instead of 0805.
- **ESD protection changed.** The 4× PESD3V3 (SOD-882, 1 × 0.6 mm) are replaced by **one SRV05-4** (SOT-23-6) across the 4 pulse lines, with VP tied to +3V3.
- **Precision values relaxed** to what is stocked: the 4–20 mA scaling resistors are 10k 1% and 4k7 5%. The firmware ADC readback trims the residual error.
- **Straight series-resistor tracks** (per your R144 example). Every series resistor and pull-up is turned so its IC-side pad faces the IC and its connector-side pad faces the connector. This gives straight vertical runs:
  - pin → 1k → 10k → 74HC165;
  - 74HC595 → 330R → pin.
- **Rev B.1 (2026-10-21):**
  - Signal tracks are routed at 0.4 mm (the Default netclass), then widened to 0.5 mm wherever clearance allows (`tools/widen.py`). Power stays at 0.5 mm. Routing everything at 0.5 mm doesn't fit on 2 layers in 100×100.
  - Every reference designator sits outside all part outlines at 0.8 mm. Resistor banks and the LED row carry a group label instead (e.g. "1K: R101-104").
  - The 4–20 mA blocks are redrawn as wired circuits.
  - J12 is now 6-pin: START, LID, SPARE1, SPARE2, GND, 3V3.
- **Port silkscreen:**
  - A direction arrow on every 5-pin bank: J1–J4 flow into the jig, J5–J8 flow out to the DUT.
  - Pin labels with the global channel number ("G 5 6 7 8" on J2/J6).
  - A bank value label ("1K / 10K x4", "330E x4").
  - Pin lists on J9–J12.
- **Sourcing BOM:** `out/fab/TestJig-BOM-sourcing.csv` gives a Quartz Components / Robu listing for every line, checked 2026-10-21.

## Blocks

| Block | Parts | Notes |
|---|---|---|
| MCU | ESP32 30-pin DevKit (U1) on 2×15 female headers | VIN = USB 5 V, 3V3 from the DevKit LDO. Pin numbering same as WM1. |
| DUT relay sense | 2× 74HC165 (U5, U6), 1k series + 10k pull-up (0805), J1–J4 | Each JST-XH5: pin1 = COM (GND), pins 2–5 = NO. Relay ON → NO closed → bit reads **0**. |
| Jig inputs | 74HC165 (U7), 4-way DIP SW1, J12 (START, LID) | DIP and buttons switch to GND, so ON/pressed reads 0. 2 spare inputs. |
| DUT input drive | 2× 74HC595 (U2, U3) @3.3 V, 330R (0805), J5–J8 | Each JST-XH5: pin1 = GND, pins 2–5 = OUT. /OE has a pull-up, so outputs are Hi-Z at boot. |
| Jig LEDs | 74HC595 (U4), 470R (0805), 8× 1206 LED | PASS, FAIL, BUSY, LORA, USER1–4. |
| Pulse I/O | J9 | PO1/PO2 via 330R (LEDC/RMT); PI1/PI2 via 1k with 10k pull-up (PCNT); SRV05-4 ESD array on all four. |
| ADC (ESP32 ADC1) | R10–R17, C16–C19 | AO1/AO2 readback ÷2, AI1 ÷2 (up to ~6 V), AI2 ÷11 (up to ~34 V, e.g. the DUT's 20 V loop rail). |
| 4–20 mA ×2 | MCP6002, MMBT3904, BC807, BAT54WS per channel | Iout = Vset × 10 mA/V from 5 V. Max load ≈ 160 Ω at 20 mA (WM1 is 150 Ω). JP1/JP2 put a local 150 Ω load on for self-test. |
| LoRa sniffer | Ra-01SH / ISC-SX1262-B (U10), SMA J13 | Same SPI/RST/BUSY pins as WM1-Pro; DIO1 is on IO15 (WM1: IO26). |

## Port naming (silkscreen = harness = firmware)
Each channel has one global number, printed on the silkscreen next to its pin. Firmware uses the same name, so nobody has to translate "J2 pin 3".

| Name | Connector / pins | Direction (silk arrow) | Meaning |
|---|---|---|---|
| `IN1`–`IN16` | J1–J4, pins 2–5 (J1 = 1–4 … J4 = 13–16); pin 1 "G" = COM/GND | into the jig | DUT relay NO contact. Reads **0 = contact closed** (relay ON) |
| `OUT1`–`OUT16` | J5–J8, pins 2–5 (J5 = 1–4 … J8 = 13–16); pin 1 "G" = GND | out of the jig | 3.3 V logic into a DUT input (330 R series) |
| `PULSE_OUT1/2`, `PULSE_IN1/2` | J9 pins 1–4 (5 = GND, 6 = 3V3) | O = out, I = in | Flow-pulse generate / count |
| `AO1`, `AO2` | J10 pins 1–2 | out | 4–20 mA source |
| `AI1`, `AI2` | J11 pins 1–2 | in | ESP32 ADC (AI1 ÷2, AI2 ÷11) |
| `START`, `LID`, `SPARE1`, `SPARE2` | J12 pins 1–4 (5 = GND, 6 = 3V3) | in | START = operator start button, LID = lid/clamp-closed or DUT-present switch, SPARE1/2 = free (all pulled up, 0 = closed to GND) |
| `DIP1`–`DIP4` | SW1 | – | Product / mode select (0 = ON) |

**Per-product mapping lives in firmware.** Each DUT gets a small table that ties its own terminal names to jig channels, selected by the DIP switch. Example for WM1:

```c
// WM1 WaterMaster: DIP = 0001
{ "RL1 pump",   IN(1) },  { "RL2 dosing", IN(2) },  { "RL3 zone1", IN(3) }, ...
{ "IN1 no-power", OUT(1) },
{ "Press_IN1",  AO(1) },  { "Press_IN2",  AO(2) },  { "Flow", PULSE_OUT(1) },
```
Test steps and logs then read like "RL3 zone1 (IN3) expected ON, got OFF". Both the DUT name and the jig channel are visible, so the harness can be checked straight from the log.

## Power-up safety (the WM1 595 issue)
- All three 74HC595 have **/OE pulled up by R1 (10k)** on GPIO4. GPIO4 is not a boot-strap pin, so the outputs stay high-impedance through power-up, boot ROM, flashing and reset. Nothing drives the DUT and no LED lights until firmware takes over.
- Firmware start-up order:
  1. shift in 24 zeros and pulse RCLK;
  2. then drive GPIO4 low (enable outputs);
  3. on any fault or reset path, drive GPIO4 high first.
- /SRCLR is tied high. The register contents are random at power-up, but /OE keeps them off the pins until step 1 has cleared them.

## Not on this board, on purpose
- **No RTC.** Test logs take their timestamp from the PC test software (or NTP over Wi-Fi). The DUT's own RTC (e.g. WM1's DS1307) is tested by reading it from the DUT.

## Shift-chain bit order (firmware)
- **Out (SER = IO13, CLK = IO12, LATCH = IO14, /OE = IO4):** U2 → U3 → U4, 24 bits, the first bit shifted lands on U4.QH. Send order: LED8…LED1 (U4 QH…QA), then OUT16…OUT9 (U3), then OUT8…OUT1 (U2). QA = lowest number.
- **In (/PL = IO2, CLK = IO12 shared, Q7 → IO15):** U5 → U6 → U7, 24 bits. Read order: bits 0–7 = IN8…IN1 (U5 D7…D0), bits 8–15 = IN16…IN9 (U6), bits 16–23 = SPARE2, SPARE1, LID, START, DIP4…DIP1 (U7).

## ESP32 pin map
| GPIO | Net | GPIO | Net |
|---|---|---|---|
| 18 / 19 / 23 / 5 | LoRa SCK / MISO / MOSI / NSS | 25 / 15 / 27 | LoRa RST / DIO1 / BUSY |
| 12 | SR_CLK (10k pull-down, strap) | 13 / 14 / 4 | 595 SER / RCLK / /OE |
| 2 / 26 | 165 /PL / Q7 (SR_IN) | 16 / 17 | Pulse out 1 / 2 |
| 21 / 22 | Pulse in 1 / 2 | 32 / 33 | PWM for AO1 / AO2 |
| 36 / 39 | ADC AO1 / AO2 readback | 34 / 35 | ADC AI1 / AI2 |

## Status (rev C, 2026-10-21)
| Check | Result |
|---|---|
| ERC | 0 violations |
| Schematic vs design netlist (`tools/check_net.py`) | 489 pins, 0 mismatches |
| DRC: unconnected items | 0 |
| DRC: clearance / silkscreen | 0 / 0 (every reference outside part outlines) |
| DRC: schematic parity | 0 |
| DRC: clearance / shorts / edge | 0 |
| Track widths | see the rev D export (signal ≥0.4 mm almost everywhere, power ≥0.8 mm) |
| DRC: remaining | 8 thermal-relief notes (1 spoke instead of 2) on through-hole connector GND pins: connected, just fewer spokes |

**Not on the board netlist** (listed at the end of the sourcing BOM): 15-pin female headers for the DevKit and M3 standoffs. The JST-XH housings and crimps for the harnesses are not listed at all.

## Please check when reviewing
1. **DevKit row spacing.** The footprint uses 25.4 mm between pin rows; WM1's footprint is about 25.0 mm. Measure your module and change `row=` in `tools/gen_lib.py` if needed.
2. **LoRa module.** The Ra-01 footprint is reused with pin 10 = BUSY, exactly as in WM1. Buy it from the same source as WM1. Quartz's LRTC-1262 is also SX1262-based but has a different pinout.
3. **DUT GND.** The jig GND and the DUT GND must be common through the harness (COM/GND pins).
4. **SMA edge mount.** J13 sits on the board edge by design; a DRC rule in `TestJig.kicad_dru` allows that.
5. **Auto-routed.** The board was routed with Freerouting. Before ordering, worth a manual pass to:
   - shorten long traces,
   - tidy the silkscreen,
   - check the current-source loops.

## Regenerate
Quick path: `tools/run_all.sh` (place, route, fix-up, widen tracks, re-check until 0 unconnected), then `tools/export.sh` (PDFs, Gerbers, BOMs, renders).

```bash
cd tools
python3 gen_lib.py && python3 gen_sch.py
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli sch export netlist --format kicadsexpr -o ../out/check.net ../TestJig.kicad_sch
python3 check_net.py ../out/check.net
KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
$KPY gen_pcb.py && $KPY route.py 80
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --schematic-parity -o ../out/drc.rpt ../TestJig.kicad_pcb
$KPY fixup.py      # maze-routes leftover pad pairs from drc.rpt and bridges isolated GND pour islands (re-run DRC + fixup until 0 unconnected)
python3 gen_bom.py # sourcing BOM with supplier links
```
Freerouting is not deterministic, so each run gives a slightly different board.
`gen_pcb.py` throws away any manual PCB edits. After hand-editing in KiCad, use **Update PCB from Schematic** instead.
