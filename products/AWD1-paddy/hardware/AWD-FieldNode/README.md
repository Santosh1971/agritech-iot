# AWD field node v0.1: segmented capacitive level strip

This board is the paddy AWD (alternate wetting and drying) field node. It is a T-shaped, 4-layer PCB.

- **Head:** 40 × 60 mm, inside a sealed 50 mm PVC end cap. It carries an ESP32-S3-MINI-1, an ISC-SX1262-B LoRa module (the same module and pin map as WPC), a LiFePO4 14500 cell and an SMA connector.
- **Strip:** 10 × 300 mm, pushed into the AWD tube.
  - 12 sensing pads, 8 × 18 mm on a 20 mm pitch, covering +7 to −17 cm.
  - One dry reference pad.
  - One GND water-return pad.

The node reports to a WPC-style Master over LoRa at 866 MHz.

## Status

| Item | State |
|---|---|
| Schematic | Complete. ERC reports 0 violations. |
| PCB ↔ schematic | 0 parity issues. |
| Outline, stack-up, zones | Done. Board is 4 layers: F pads / In1 GND (hatched 25 % under the pads) / In2 sense lanes + GND / B GND. |
| Strip routing | **Done.** Each pad → via → In2.Cu lane (0.15 mm, 0.6 mm pitch) → 45° fan-out → RS series resistor. |
| Head placement | Placed next to the pins each part serves. J3 (battery JST) is on the back, under the SX1262. |
| Head routing | **Done** by Freerouting 1.9.0 (passes in `route/`) on F.Cu and In2.Cu, with GND stitching vias added by `tools/stitch.py`. Hand-routed nets: R4's +3V3 (via → B.Cu → J2 pin 1); the SX1262 VDD feed (B.Cu at x = 107.4 mm), which clears the straight 50 Ω antenna line. |
| Remaining DRC | **U1 pad 63** (an ESP32 corner GND pad) sits in a small F.Cu GND island. The touch fan-out fills that corner, so no via fits. It is harmless because the module ties its GND pads together internally. **J1** is an edge-launch SMA and touches the edge on purpose; check its seating against the Amphenol 132289 datasheet. The rest are silkscreen cosmetics and three single-spoke thermals. |

## Pin map (ESP32-S3)

| Function | GPIO |
|---|---|
| Touch T1–T12 → pads P1–P12 | IO1–IO12 |
| Touch T13 → REF pad | IO13 |
| T14 spare (to TP1) | IO14 |
| VBAT_ADC (ADC2_CH4) / VBAT_EN | IO15 / IO16 |
| LoRa DIO1 / BUSY / RST | IO17 / IO18 / IO21 |
| LoRa NSS / SCK / MOSI / MISO | IO39 / IO40 / IO41 / IO42 |
| LED | IO38 |
| PAIR/BOOT button | IO0 |
| USB D−/D+ and UART0 | J2 (1.27 mm pogo/header) |

## AWD thresholds

| Action | Condition |
|---|---|
| Pump OFF (+5 cm) | P2 wet |
| Pump ON (−15 cm) | P11 dry |
| SOIL line (0 cm) | Printed on both faces as the installation depth mark |

## Regenerate

The files are generated from `tools/design.py`, which defines the parts, nets and geometry.

```bash
cd products/AWD1-paddy/hardware/AWD-FieldNode
python3 tools/gen_sch.py      # schematic, AWD.kicad_sym, tools/netlist.json
python3 tools/gen_fp.py       # AWD.pretty pad footprints
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/gen_pcb.py
```

**Warning:** `gen_pcb.py` rebuilds the board from scratch and **throws away the head routing**. Now that the board is routed, make changes in KiCad: edit the schematic, then run *Update PCB from Schematic*.

### Re-routing the head and watching it

To watch the autorouter work:

1. Unlock the strip tracks if you need to move them; leave them locked otherwise.
2. Remove the `GND_top_head` and `GND_in2` zones.
3. Run *File → Export → Specctra DSN*.
4. In the `.dsn` file, change `In1.Cu` (and optionally `B.Cu`) to `(type power)`.
5. Run:

```bash
java -jar ~/Applications/freerouting/freerouting-1.9.0.jar -de route/AWD-FieldNode.dsn
```

6. Press **Autorouter** in the Freerouting window to watch it route.
7. Save the session, then run *File → Import → Specctra Session* in KiCad.
8. Refill the zones (B).

## Fab notes

- **Order with JLCPCB stack-up JLC04161H-7628** (L1–L2 prepreg 0.21 mm, εr ≈ 4.4). The antenna feed (`/RF_ANT`, SX1262 pad 1 → J1) is a straight 10.9 mm, 0.36 mm trace on F.Cu over the In1 GND plane: about 50 Ω. `AWD-FieldNode.kicad_dru` keeps the F.Cu pour 0.5 mm away from it. To hold ±10 %, tick impedance control.
- `fab/` holds gerbers + drills (`AWD-FieldNode-gerbers.zip`), BOM and CPL. These were exported with kicad-cli; regenerate them after any board change.

- 4 layers, 1.6 mm FR4, ENIG finish, solder mask on both sides, vias tented.
- The pads have **no mask opening**. Below the neck the strip carries no exposed copper.
- After assembly, dip-coat the strip in 2-part epoxy or PU, 100–200 µm thick, and seal the routed edges.
