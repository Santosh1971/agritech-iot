# WM-Mini C3 rev A: pump, doser and 4-valve controller (2026-10-27)

A larger FG1. An ESP32-C3 Super Mini (plug-in, USB-C powered) drives six SRD relays through a ULN2003A:
- **K1 PUMP** and **K2 DOSER** switch 230 V.
- **K3–K6** switch 12/24 VAC valves, with one common.

It also has a Hall flow input, a DS1307 RTC with a CR2032, and seven LEDs. The plan and the decisions behind it are in [SPEC.md](SPEC.md) (draft 0.3) and on the "WM-Mini C3 Plan" page.

## Status

| Item | State |
|---|---|
| Schematic | Generated from `tools/design.py`, with wires inside each block and labels only between blocks. **ERC: 0 errors, 0 warnings.** |
| PCB | 99 × 48 mm, 2 layers. Routed with Freerouting, zones filled. **DRC at every severity: 0 violations, 0 unconnected, schematic parity clean** (kicad-cli 9.0.7). |
| 230 V | 5 mm from the pump/doser copper (including their NC pins, which are live while off) to all other copper, enforced by `WM-MiniC3.kicad_dru`. The exceptions are inside K1/K2: 3.5 mm to their own coil pins (the SRD's pinout gives 3.6 mm), and 2.4 mm between pump and doser (the 5.08 mm header has 2.48 mm between its own pads). |
| Panel | `panel/WM-MiniC3-panel.kicad_pcb`, 99 × 98 mm: 2 boards and a 2 mm routed slot with 3 plain tabs (no V-cut, no mouse bites). Board 1 is turned 180°, so both connector edges face outwards. |
| Fab | `fab/`: panel and board Gerbers, schematic PDF, 1:1 prints (top and bottom), BOM, STEP and renders. **Order `WM-MiniC3-panel-gerbers.zip`.** |

## Layout

The board is the draft 0.3 plan **mirrored left to right**. On an upright SRD the NO pin comes out on the left. With the 230 V corner on the left, the pump and doser NO and COM run straight down into their headers without crossing, and still keep 5 mm from the valve header.

- **Bottom left:** K1 PUMP and K2 DOSER, upright, directly above J2 and J3.
- **Top left, above the relays they drive (all > 5 mm from 230 V):**
  - the ULN2003;
  - the six relay LEDs (P D V1 V2 V3 V4) and their resistors;
  - C1, 1000 µF / 16 V (10 mm, 5 mm pitch).
- **Middle, valve block:**
  - K3 V1 and K4 V2 are inverted at the top edge.
  - K5 V3 and K6 V4 are upright below them.
  - All coils meet near the centre line.
- **Top right, next to the ESP:**
  - the CR2032 standing vertical;
  - the DS1307, a through-hole C26 crystal and the I2C pull-ups;
  - the FLOW LED and test pad TP1.
- **Bottom right:** the Super Mini, with USB-C at the bottom edge. Under the module sit the flow-input parts (B5819W, SLCESDB5V0D5 bidirectional 5 V TVS, SOD-523 on hand-solder pads, 10k). IO2 is unused (the AUX pull-up and test pad were removed).
- **+5 V:** 1 mm tracks from the module to all relay coils.
- **Bottom edge, left to right:** J2 PUMP (NO, COM), J3 DOSER (NO, COM), J4 VALVES (COM, V1, V2, V3, V4), J1 FLOW (5V, GND, SIG), USB-C.
- **Holes:** 2 × M3 at (3.5, 24) and (95.5, 24), 92 mm apart, as on the ESP32 test jig.

## Pin map

| GPIO | Net | Function |
|---|---|---|
| IO20 | RLY_PUMP | K1 pump, ULN in 6 / out 11 |
| IO10 | RLY_DOSER | K2 doser, ULN in 5 / out 12 |
| IO1 | RLY_V1 | K5 valve 1, ULN in 4 / out 13, LED D6 |
| IO0 | RLY_V2 | K3 valve 2, ULN in 1 / out 16, LED D7 |
| IO3 | RLY_V3 | K4 valve 3, ULN in 2 / out 15, LED D8 |
| IO4 | RLY_V4 | K6 valve 4, ULN in 3 / out 14, LED D9 |
| IO2 | — | unused (strapping pin, left floating as on Mini-C3) |
| IO5 | FLOW | flow pulses |
| IO6 / IO7 | SDA / SCL | DS1307 |
| IO21 | LED_FLOW | flow LED |
| IO8 | — | on-module LED = WiFi LED |
| IO9 | — | BOOT button = setup button |

## Parts that are not stock KiCad (`ASC.pretty`)

| Part | Footprint |
|---|---|
| Pluggable headers | `Pluggable_2EDGR_5.08_1x0{2,3,5}_Horizontal`: the stackable market header (2EDGR / KF2EDGR), body n × 5.08 mm. It has MSTBA pitch and drills, with round 2.6 mm pads. The pin row is 2.5 mm from the edge; the plug overhangs the board edge. |
| Relays | `Relay_SPDT_SANYOU_SRD_Outline`: KiCad's SRD Form C with the silkscreen cut to the outline, so the K1 PUMP etc. labels fit inside |
| Super Mini | `ESP32-C3_SuperMini_Socket`: from ASC Mini-C3, without its outline silk (that ran past the board edge) |
| CR2032 | `Battery_CR2032_Vertical_3pin`: from ASC Mini-C3 |
| Flow TVS | `D_SOD-523_HandSolder`: SOD-523 with 1.2 × 1.0 mm pads at 2.0 mm pitch for hand soldering (SLKOR SLCESDB5V0D5) |

## Check before ordering

1. **Print `fab/WM-MiniC3-print-1to1.pdf` at 100 %.** Lay on it a real SRD relay, a 2EDGR header, the CR2032 holder and the Super Mini. Pin sides: IO5 and 5V must be at the USB end.
2. **SRD NO pin.** Pad 3 is NO, as on WM1. Check one Rajiv relay (RERL-32) with a meter.
3. **CR2032 holder pins.** The footprint is the Mini-C3 tabbed-cell layout, and its − leg position is provisional. Measure the RBH-2 holder.
4. **C1 size.** The footprint is 10 mm diameter, 5 mm pitch: check your 1000 µF part.
5. **Plug overhang.** The plugs stick out about 8 mm past the bottom edge. Check this against the box wall.
6. **The Super Mini plugs in after the board is screwed down.** Its antenna end sits over the right M3 screw head; a DRC rule ignores that overlap on purpose.

## Regenerate

```bash
cd products/WM1-watermaster/hardware/WM-MiniC3
zsh tools/build.sh 5     # schematic + ERC, then place / route / DRC until clean (Freerouting is not deterministic)
zsh tools/fab.sh         # panel + fab/
```

`tools/clearance_check.py` and `tools/layout_svg.py` are the draft 0.3 planning helpers. They describe the unmirrored plan.
