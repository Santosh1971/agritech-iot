# ASC Mini-C3 rev B: the low-cost Mini, shared with FG1

This is the cost-down Mini for the ASC Student Kit (2026-10-10). The same board is meant to become the next **FG1 (FlowGuard)**: it has FG1's flow input, relay, RTC and LEDs. It does not replace rev A (`../Mini`, ESP32-S3 module, 97 × 47 mm).

Two boards share one 100 × 100 order:

| Board | Size | What it is |
|---|---|---|
| **ASC-MiniC3** | 48 × 44 mm | Carrier for a plug-in ESP32-C3 Super Mini. It has a flow input, a 5 V pump driver, a 230 V relay, a DS1307 RTC, I2C and AUX terminals and four LEDs. |
| **ASC-AHT20** | 48 × 6 mm | A T/RH "sensor stick" on a 4-core cable, so the sensor sits outside the box and students can breathe on it |

**The panel (`fab/ASC-MiniC3-panel-gerbers.zip`, 96 × 100 mm, order this one):**
- It holds 4 Minis (2 × 2) and 4 sticks (2 × 2 underneath).
- V-cuts sit at x = 48 and at y = 44, 88 and 94. Every cut runs edge to edge.
- The cut lines are on the **Cmts.User** layer, so tell the fab: "V-cut as per the Cmts layer".
- Copper keeps ≥ 0.4 mm from every edge.
- At WhyPCB's quote (20 panels for ₹3,000), one Mini plus one stick costs **₹37.50** in PCB.

## Status

| Item | State |
|---|---|
| Schematics | Both are generated from `tools/design.py` and `tools/design_aht20.py`. **ERC: 0 errors, 0 warnings** on both. |
| PCBs | Both are routed (Freerouting) with zones filled. **DRC at every severity: 0 violations, 0 unconnected, schematic parity clean** on both (kicad-cli 9.0.7). The panel's DRC shows 0 errors. |
| 230 V | **5.3 mm** measured from relay copper to anything else. The DRC rule in `ASC-MiniC3.kicad_dru` requires ≥ 5 mm. Inside the relay, coil and contacts are 9.6 mm apart. |
| Fab | Panel, Mini and stick Gerbers. JLC BOM/CPL for each board and for the panel (unique designators R1_1 … R1_4). Schematic PDFs, 1:1 prints and renders. Run `tools/fab.sh` to regenerate. |
| Costing | `costing.csv`: board plus stick about ₹267, full kit about ₹1,026 (IP-65-26, YF-S401 at ₹230, HF46F at ₹42, 3 standoffs) |
| Mounting | 3 × 4.2 mm holes, arranged in a triangle, for 5 mm nylon reverse-mount snap standoffs (Probots PRO5803, ₹9.40 at 50+). Each standoff's flat base sticks to the box floor with double-sided foam tape; the snap head goes through the PCB hole. This works in any box with a flat floor; the box's own bosses are not used. Enclosure: REI IP-65-26 (90 × 58 × 44). |

## Pin map (ESP32-C3 Super Mini)

| GPIO | Function | Notes |
|---|---|---|
| IO0, IO1 | AUX A0, A1 | ADC1 inputs |
| IO3 | PUMP | AO3400A low side. R2 100 k holds it off through reset. |
| IO4 | AUX D | 10 k pull-up. For DHT11/22 or a DS18B20 probe. |
| IO5 | FLOW | YF-S401 (5880 pulses per litre) or YF-S201 (450 per litre). Level-shifted by D2 + R6. |
| IO6, IO7 | SDA, SCL | DS1307 (0x68) and AHT20 (0x38). 4.7 k pull-ups to 3V3. |
| IO10 | STATUS LED | FG1 uses it as the WiFi/status LED |
| IO20 | RELAY | AO3400A low side, R11 holds it off. This pin is UART0 RX, an input at boot, so the relay stays off while the board boots. |
| IO2, IO8, IO9, IO21 | free | IO2/8/9 are strapping pins (IO8 = module LED, IO9 = BOOT). IO21 is UART0 TX. |

## Connectors

| Ref | Connector | Pins |
|---|---|---|
| J5 | I2C, 4-pole 3.81 mm (top edge) | 3V3, GND, SCL, SDA. This is the AHT20 stick's order and the usual VIN GND SCL SDA module order. |
| J4 | FLOW, JST-XH 3-pin (top right). Takes the YF-S401's plug directly. | GND, 5V, SIG |
| J3 | AUX, 5-pole 3.81 mm (bottom edge) | 3V3, D, A0, A1, GND |
| J1 | PUMP, 2-pole 3.81 mm (bottom edge) | P+ (5 V), P− (low side) |
| J6 | RELAY 230 V, 2-pole 5.08 mm (bottom right) | COM, NO. HF46F, 5 A 250 VAC, for a pump contactor coil. |

## LEDs (seen through the clear lid)

| LED | How it's driven |
|---|---|
| PUMP (red) | Hardware: on the pump driver |
| FLOW (yellow) | Hardware: blinks with every flow pulse |
| STATUS (green) | GPIO10 |
| RELAY (blue) | Hardware: across the relay coil |

## Moving FG1 onto this board

1. **Firmware.** FG1 runs on a classic ESP32 today (`esp32dev`). It needs a C3 build with these pins: RELAY IO20, FLOW IO5, I2C IO6/IO7, status LED IO10.
2. **Flow LED.** It no longer needs a GPIO, so FG1's `FLOW_SENSOR_LED_PIN` goes away.
3. **Flow sensor.** Set `DEFAULT_PULSES_PER_LITER` to suit the sensor: 450 for a YF-S201, 5880 for a YF-S401.
4. **Power.** In the field, FG1 runs from a 5 V / 1 A USB-C charger plugged into the Super Mini.
5. **Relay.** The relay is an HF46F (5 A), not FG1's SRD (10 A). That is ample for a contactor coil. The SRD's COM pin sits between its two coil pins, which made 5 mm clearance impossible on a 48 mm board.

## Check before ordering

1. **Super Mini footprint.** Print `fab/ASC-MiniC3-print-1to1.pdf` at 100 % and lay a real Super Mini on it. The rows should be 15.24 mm apart. The silkscreen marks IO5 and 5V at the USB end and IO21 and IO0 at the far end, so a mirrored module is easy to see.
2. **Tabbed CR2032.** The two + legs are 15.2 mm apart. The − leg offset of 4.0 mm is a guess, so measure one cell.
3. **YF-S401 plug order.** The board expects GND, 5V, SIG, with the red wire in the middle.
4. **HF46F footprint.** It is the same footprint as rev A (Hongfa datasheet). Push a real relay through a 1:1 print.
5. **AHT20 footprint.** It follows the ASAIR datasheet V1.0.03, figure 8: 0.8 × 0.5 mm pads at 1.0 mm pitch, rows 2.0 mm apart. Pins: 2 VDD, 3 SCL, 4 SDA, 5 GND.
6. **Assembly on the panel.** JLC assembly normally wants 5 mm rails, which would make the panel 96 × 110 mm. Either assemble by hand (every passive is 0805; the AHT20 needs hot air or a stencil) or ask WhyPCB whether they can place parts on a panel with no rails.
7. **USB power.** The pump (0.2–0.3 A), the relay coil (~70 mA) and the C3 (up to ~350 mA) all run from USB 5 V. Use a 1 A charger.

## Regenerate

```bash
cd products/ASC-StudentKit/hardware/MiniC3
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
for d in design design_aht20; do
  DESIGN=$d python3 tools/gen_sch.py      # schematic, ASC.kicad_sym, tools/netlist-<PROJECT>.json
  DESIGN=$d $KP tools/gen_pcb.py          # placed board (plus the locked 230 V tracks)
  DESIGN=$d $KP tools/route.py            # Freerouting 1.9 (~/Applications/freerouting), then zone fill
done
kicad-cli pcb drc --schematic-parity --severity-all -o /tmp/drc.rpt ASC-MiniC3.kicad_pcb
$KP tools/panel.py                        # panel/ASC-MiniC3-panel.kicad_pcb
zsh tools/fab.sh                          # fab/
```

Freerouting is not deterministic. If DRC reports unconnected items, run `gen_pcb.py` and `route.py` again; a clean result usually comes within a few runs.

How the routing script handles the special nets:
- The two 230 V tracks are drawn and locked by `gen_pcb.py`. Freerouting cannot place them, because the terminal's own pins are closer together than the 5 mm class clearance.
- The GND pours are taken out of the DSN export, so GND is also routed as tracks.
- The no-pour areas are taken out of the export too, because the exporter would turn them into full keep-outs.
