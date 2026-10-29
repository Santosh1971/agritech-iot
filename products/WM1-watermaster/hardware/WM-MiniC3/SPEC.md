# WM-Mini C3: specification, pin map and tentative layout (draft 0.3, 2026-10-27)

WM-Mini C3 is a larger FG1: one ESP32-C3 Super Mini runs a pump, a doser pump and 4 valves on a time or litre schedule. It reuses the Mini-C3 (FG1 rev B) circuits for the C3, flow input, DS1307 and ULN2003, and the WM1 Mini relay roles and scheduling rules.

Status: **rev A schematic and PCB done (2026-10-27)**; see [README.md](README.md). The PCB is this plan mirrored left to right (230 V corner on the left, Super Mini on the right) so the SRD's NO pin feeds its header without crossing.

Files:
- `wm-mini-c3-plan.html` is the published page (https://claude.ai/artifact/8Yds3n2hrs27nZd5SjbgLB).
- `layout_svg.py` draws the to-scale layout.
- `clearance_check.py` checks pad-to-pad spacing between voltage classes.

## Changes

**0.3 (Santosh's layout review):**
- Valves are **12/24 VAC**. Only the pump and doser are 230 V.
- The ULN2003 and DS1307 go **under the Super Mini**.
- The top row of relays is **inverted** and pushed to the top edge, so all coils meet near the centre line.
- The relays sit closer together.
- The connectors are pushed to the edge.
- Result: back to **99 × 48**, two boards per panel.

**0.2:**
- WiFi LED moved to IO8 (onboard).
- Relays changed to SRD-05VDC-SL-C.
- Two M3 holes, as on the test jig.
- All cables on one edge.
- Flow connector changed to 3-pin 5.08 mm.

## 1. Board summary

| Item | Spec |
|---|---|
| MCU | ESP32-C3 Super Mini on 2 × 8 female sockets, USB-C at the bottom edge |
| Under the module | U3 ULN2003A (SOIC-16), U2 DS1307 (SOIC-8), Y1 32.768 kHz (3215), 2 × 4K7, all in the 12.7 mm gap between the sockets |
| Power | USB-C 5 V via the Super Mini's 5V pin, 5 V / 2 A adapter, C1 470 µF / 10 V |
| RTC backup | CR2032 vertical holder (Rajiv RBH-2 type), top-right |
| Relays | 6 × SRD-05VDC-SL-C, 5 V (~72 mA), SPDT, 7 A 250 VAC (Rajiv RERL-32, ₹14.20). Footprint `Relay_THT:Relay_SPDT_SANYOU_SRD_Series_Form_C`: pad 1 COM, 2/5 coil, 3 NO (as WM1), 4 NC unused. |
| Loads | K1 pump: 230 V contactor coil. K2 doser: 230 V. K3–K6 valves: 12/24 VAC solenoids. |
| Relay LEDs | 1206 + 1 k across each coil, upright in a row at the top-left |
| Flow | Hall-effect, Schottky + pull-up level shift, PESD3V3 |
| Connectors | Phoenix MSTBA 2,5 / 5.08 mm horizontal + plugs. Pin row 2.5 mm from the bottom edge. |
| PCB | 99 × 48 mm, 2-layer, 1.6 mm. 2 boards + 2 mm routed slot = 99 × 98 panel. |
| Mounting | 2 × M3 at (3.5, 24) and (95.5, 24), 92 mm apart, as the ESP32 test jig |

## 2. Pin assignment (unchanged from 0.2)

| GPIO | Net | Function |
|---|---|---|
| IO20 | RLY_PUMP | K1 pump, ULN in 6 / out 11 |
| IO10 | RLY_DOSER | K2 doser, ULN in 5 / out 12 |
| IO1 | RLY_V1 | K5 valve 1, ULN in 4 / out 13, LED D6 |
| IO0 | RLY_V2 | K3 valve 2, ULN in 1 / out 16, LED D7 |
| IO3 | RLY_V3 | K4 valve 3, ULN in 2 / out 15, LED D8 |
| IO4 | RLY_V4 | K6 valve 4, ULN in 3 / out 14, LED D9 |
| IO2 | — | unused (strapping pin, left floating as on Mini-C3) |
| IO5 | FLOW | Flow pulses |
| IO6 / IO7 | SDA / SCL | DS1307, 4.7 k to 3V3 |
| IO21 | LED_FLOW | Flow LED |
| IO8 | LED_WIFI | Onboard LED |
| IO9 | — | BOOT button = setup button |
| IO18/19 | USB | Flashing, serial |

ULN channel 7: input to GND.

## 3. Layout (99 × 48, origin top-left, wiring edge = bottom)

| Block | Position (mm) |
|---|---|
| LED row (P, D, V1–V4, F), resistors below | x 2.4–19, y 2.6–8.5 |
| C1; flow parts, 4K7s, IO2 pad | (6.2, 14); x 11–18.6, y 10–18 |
| U1 Super Mini (ULN, DS1307, Y1 under it) | x 1–19, y 25.5–48 |
| K3 V1, K4 V2: **inverted** (contacts at the top edge) | centres x 27.5 / 45.5, y 0.5–20.1 |
| K5 V3, K6 V4: upright | centres x 27.5 / 45.5, y 22.4–42 |
| K2 DOSER, K1 PUMP: upright, nothing above them | centres x 66.5 / 84.5, y 22.4–42 |
| BT1 CR2032 | x 66–88, y 3.5–10.5 |
| Pump/doser coil feed | along y 14.5, then straight down to the coil pins |
| J1 FLOW / J4 VALVES / J3 DOSER / J2 PUMP | x from 20 / 35.8 / 64.0 / 79.3; pins at y 45.5 |
| Holes | (3.5, 24), (95.5, 24) |

**Coils:** all six relay coils sit around y 17–26, near the centre line. The +5 V coil supply is common to all. The four valve COMs join there and go down to J4's COM pin.

**Relay gaps:** 2.6 mm everywhere, except 5.6 mm between the valve block and the 230 V corner.

**Spacing, pad to pad, from `clearance_check.py`:**

| Between | Closest | Target |
|---|---|---|
| 230 V ↔ 12/24 VAC (doser NC ↔ J4 V4) | 5.0 mm | ≥ 5 |
| 230 V ↔ 5 V | 9.4 mm | ≥ 5 |
| Pump ↔ doser | 3.0 mm | ≥ 3 |
| 5 V ↔ 12/24 VAC (J1 ↔ J4) | 3.0 mm | ≥ 1 |
| Inside each SRD, coil ↔ COM | 3.6 mm | relay's own |

## 4. Power budget

| Load | mA |
|---|---|
| 6 coils, all on | 430 |
| C3 WiFi peaks | 350 |
| LEDs + flow sensor | 35 |
| **Peak** | **≈ 815** (use 5 V / 2 A) |

## 5. Cost (relays + PCB per board)

| Version | Total | Relays | PCB |
|---|---|---|---|
| **0.3: SRD, 99 × 48, 2 per panel** | **≈ ₹185** | ₹85 | ₹100 |
| 0.2: SRD, 99 × 62, 1 per panel | ≈ ₹285 | | |
| 0.1: HF46F, 98 × 48 | ≈ ₹352 | | |

## 6. Firmware outline

- FG1 C3 base with the WM1 Mini scheduler: programs → sequences → valves, run by time or litres.
- The pump runs only with at least one valve open; it stops when the last valve closes. Suggested: open the valve about 3 s before the pump starts, and stop the pump about 3 s before the last valve closes.
- Doser: timed (start, middle or end of a sequence), only while the pump runs.
- RTC polled every minute, with NTP re-sync. Progress is saved to flash every minute and on every change, and resumes after a power cut.
- No-flow protection. All relays off at boot.

## 7. Still open

1. **Doser voltage:** 230 V (confirmed 2026-10-27).
2. **Plug overhang:** with the pin row 2.5 mm from the edge, the plugs overhang about 8 mm. Check the box wall.
3. **Socket height:** check it on a real Super Mini, so the parts underneath clear the module.
4. **Pin checks:** Super Mini pin sides on a 1:1 print; SRD NO pin with a meter.
