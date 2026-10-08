# ASC Mini rev A: schematic

The Mini carrier board for the ASC Student Kit. It is a half board, **97.4 × 47.0 mm**, held by **3 × M3** on the box's upper three bosses. Two Minis fit on one 100 × 100 mm panel (kit architecture §8).

## Status

| Item | State |
|---|---|
| Schematic | **First draft, generated.** 92 parts, 57 nets. `tools/check_design.py` and `tools/check_sheet.py` pass. **KiCad's ERC has not been run yet** (no KiCad 9 in the build container). Run it on the Mac first. |
| Pin map | Matches `../pinmap.json` (board `mini`), checked by `tools/check_design.py` |
| Footprints | All 26 exist in the KiCad 9.0.9 library. The Grove socket uses a JST-PH 4-pin footprint as a stand-in; swap it for the HY2.0-4P part at layout. |
| PCB | Not started. Outline and holes are in `tools/design.py` (`BOARD_W`, `BOARD_H`, `HOLES`) and `../mini-pcb-outline-1to1.svg`. |

## What is on it

| Block | Parts | Notes |
|---|---|---|
| 12 V input | J1 2-way 5.08 mm terminal, D1 SS34 (reverse polarity), D2 SMAJ26A (TVS), C1–C3 | **9–24 V in.** A terminal rather than a barrel jack, so the cable comes through a gland. |
| 12 V → 5 V | U2 TPS54202 buck (28 V max, 2 A), L1 10 µH, R1/R2 set **5.3 V**, D3 SS34 | The diode drop leaves about 5.0 V on +5V |
| USB-C | J2, R3/R4 5.1 k CC pull-downs, F1 0.5 A PTC, D4 SS34, U3 USBLC6-2SC6 ESD | Flashing and the console over native USB (GPIO19/20); also powers the board on a desk. D3 and D4 OR the two 5 V sources. |
| 5 V → 3.3 V | U4 AP7361C-33E (1 A LDO) | ESP32-S3 WiFi peaks of ≈350 mA, plus the RTC and the 3.3 V sensors |
| MCU | U1 ESP32-S3-MINI-1-**N8** | EN: R8/C14 plus a **RESET** button. GPIO0: R9 plus the **PAIR/BOOT** button. |
| Board ID | R10 30 k / R11 10 k = 0.25 × 3V3 on GPIO11 | The firmware reads this as "mini" |
| 12 V sense | R5 100 k / R6 10 k on GPIO10 | 24 V reads 2.18 V; 0 V means USB power only |
| RTC | U5 DS3231MZ+ (SOIC-8), BT1 CR2032 **on the back**, INT/SQW → GPIO16 | Internal I²C bus (GPIO12/13, 4.7 k pull-ups) |
| Sensor ports S1–S4 | J4–J7 JST-XH 4-pin: 1 GND · 2 3V3 · 3 5V · 4 SIG | SIG: 470 Ω in series, BAT54S clamps to 3V3 and GND, so a 5 V digital signal such as a flow sensor is safe. Port supplies fused: F2 0.5 A (5 V), F3 0.2 A (3V3). |
| I2C-1 student port | J8 Grove pinout: 1 SCL · 2 SDA · 3 3V3 · 4 GND | Its own bus (GPIO14/15), 4.7 k pull-ups |
| Relays OUT1/OUT2 | K1/K2 SRD-05VDC-SL-C, Q2/Q3 AO3400A, D12/D13 flyback, J9/J10 3-way terminals (COM · NO · NC) | GPIO35/36, gate pull-downs so the relays stay off through reset. **LOW VOLTAGE ONLY** on the silkscreen. |
| Buzzer | BZ1 5 V active 12 mm, Q1 AO3400A, D7 | GPIO46 is a strapping pin: R14 holds it low at reset |
| LEDs, seen through the clear lid | D5 **PWR**, D6 **STATUS** (GPIO48), D14 **OUT1**, D15 **OUT2** (on the relay coils, so they need no GPIO) | Each LED's name goes on the silkscreen (the `Silk` field on each part) |
| Test header | J3 1.27 mm × 8: 3V3, GND, EN, PAIR, D−, D+, TX, RX | The production tester's pogo pins, the same set as AWD1 |
| Mounting | H1–H3 M3 | Positions in `design.py` |

## Please review

1. **Input voltage limit.** The TPS54202 is rated 28 V, and the SMAJ26A only starts clamping above 28.9 V. That is fine for 12 V and 24 V adapters. If a site might see more than 24 V, switch to a 40 V buck.
2. **Relay size.** The SRD-05VDC is the relay WM1 uses, but it is large: two of them take about 600 mm² of a 4,400 mm² board. A slim relay (Hongfa HF46F) would free space if the layout gets tight.
3. **Connector row.** The bottom edge has room for about 78 mm of connectors between the screw bosses. The three terminals (OUT1, OUT2, 12 V IN) need about 41 mm. The four XH ports and the Grove socket need about 58 mm more. Proposal: terminals along the bottom edge, and the XH and Grove sockets (vertical) in a second row just above them, with cables running down past the edge. USB-C goes on a side edge.
4. **No OLED header on the internal bus.** The architecture listed one, but the firmware and the studio treat the OLED as a block on the student I2C-1 port. Skipping it saves a connector.

## Regenerate

```bash
cd products/ASC-StudentKit/hardware/Mini
python3 tools/gen_sch.py          # ASC-Mini.kicad_sch and tools/netlist.json
python3 tools/check_design.py     # pins vs pinmap.json, nets, power, strapping pins
python3 tools/check_sheet.py      # no accidental connections or overlaps on the sheet
```

`gen_sch.py` reads KiCad's symbol libraries from `KICAD_SYMBOL_DIR`. The default is KiCad 9 on macOS (`/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols`).

Then open `ASC-Mini.kicad_pro` in KiCad 9 and run **Inspect → Electrical Rules Checker**.

Edit `tools/design.py` and regenerate, rather than editing the schematic by hand, until the board layout starts. After that, switch to editing in KiCad, as AWD1 did.
