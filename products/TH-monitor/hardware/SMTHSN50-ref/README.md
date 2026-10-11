# SMTHSN50 reference (Sasya Systems, Dec 2021)

These are the design files from the old LoRa soil-moisture/TH node ("pro mini weather v1.2", KiCad 5.1.9), recovered from the old hard disk on 2026-10-09. TH2 uses them for the sled outline and the battery-holder position only.

- `gerbers/` holds the fab outputs.
- `SMTHSN50_Schematic.pdf` is the schematic. It shows a Pro Mini, DRF1262T/DRF1276G LoRa, a TP4056 solar charger at 130 mA, an HT7333, a 3-channel soil-moisture input, and an AM2302 on J7.

## Mechanical facts taken from the Gerbers

| Item | Value |
|---|---|
| Outline | 103.8 × 42.0 mm rectangle, no mounting holes (the sled slides into the pipe) |
| 18650 holder | 2 pins, 1.8 mm drill, **72.0 mm apart**, at x = 11.6 and 83.6 mm from the left end, **10.9 mm from the top long edge** |
| 2×8 IDC header (J4) | x ≈ 90.2–92.8 mm, 2.54 mm pitch |
