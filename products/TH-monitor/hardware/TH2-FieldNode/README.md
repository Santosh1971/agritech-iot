# TH2 field node v0.1: low-cost T/RH monitor

This board is a battery-powered temperature and humidity monitor for pest and disease alerts. It is built around four parts:

- **MCU:** ESP32-C3-WROOM-02, with WiFi upload plus BLE sync to the farmer's phone.
- **Sensor:** SHT40, on the back of the one PCB, behind a vent in the box, which sits inside the radiation shield.
- **Battery:** LiFePO4 14500 (AA size), in a holder on the PCB.
- **Charging:** USB-C into a CN3058E charger.

Target cost is under ₹1,000 landed, for a ₹2,500 MRP.

**One PCB, two builds:**

| Build | Fitted | Link | BOM (approx.) |
|---|---|---|---|
| **TH2-W** | Everything except U6, AE1, C13, C14 | WiFi upload, with BLE phone sync as fallback | ~₹900 |
| **TH2-L** | Everything, plus the SX1262 LoRa module (U6) and the 866 MHz spring antenna (AE1) | LoRa to a mains-powered master next to the farmhouse router | ~₹1,250 |

TH2-L is for nodes deep inside the crop, out of WiFi range. One master serves many nodes (see *Placement*). Soil moisture will be a separate model: the C3 has no free pins left.

## Status

| Item | State |
|---|---|
| Schematic | Complete (with the LoRa option). 50 parts. ERC reports 0 violations, and every net was checked in the exported netlist. |
| PCB | **Not started.** Outline fixed at 40 × 60 mm (common AWD box). |
| Firmware | Not started. Start from `../../firmware/th-mqtt` and `th-logger`. |

## Design decisions (and why)

| Decision | Why |
|---|---|
| **ESP32-C3-WROOM-02** (not C3-MINI-1) | KiCad ships the symbol and footprint. Castellated pads are easy to hand- or local-assemble. Same chip, similar price. |
| **LiFePO4 14500 (~600 mAh), AA holder on the PCB** | Fits the common AWD box (decided 2026-10-09). LiFePO4 survives heat with no fire risk, and the box is shaded inside the shield. About 10 months with 3-hourly WiFi uploads, about 18 months on LoRa. |
| **CN3058E linear charger** | True 3.6 V LiFePO4 CV. No inductor (simpler than TP5000). ICH = 1218 V / R3: 3.9 k gives 0.31 A (0.5C for the 14500), so about 2.5 h to charge. |
| **RT9080-33 LDO (2 µA Iq)** | The charger holds VBAT at 3.6 V ±1 %, which is the ESP32-C3 *absolute maximum*. The LDO keeps the C3 at ≤3.3 V. On the 3.2–3.3 V plateau it sits in dropout and passes VBAT straight through, so it costs almost nothing. |
| **32.768 kHz crystal on IO0/IO1**, no RTC chip | About 20 ppm (~2–4 s/day) vs ±5 % for the internal RC oscillator. A DS1307 can't be used: it needs a 4.5–5.5 V supply. |
| **USB-C does charging *and* flashing** | The C3's native USB-Serial/JTAG is on IO18/IO19. J2 carries the same signals for a magnetic pogo connector in the box wall. Fit J1 *or* J2. |
| **LoRa option on the same PCB** | Same ISC-SX1262-B module and Ra-01 footprint as WPC/AWD, so one gateway firmware covers every product. A LoRa packet costs about 45 mA × 0.2 s, against about 150 mA × 3 s for a WiFi connection, so a charge lasts about 18–24 months. |
| **Freeing pins for LoRa** | The status LED is gone (it can't be seen through the PVC). The battery divider is always on (2.2M/2.2M, 0.8 µA). UART0 now carries SPI, so logs and flashing go over native USB. The SX1262's NRESET is tied to EN. |
| **VBUS_DET on IO5** | IO5 is an RTC GPIO, so plugging in a charger wakes the C3 (it can sync and blink). The R11/R12 divider draws 0 µA when unplugged. |
| **Charge LEDs run off VBUS** | RED = charging, GREEN = full. Visible without firmware, and no battery drain. |

## Pin map (ESP32-C3-WROOM-02)

| GPIO | Net | Notes |
|---|---|---|
| IO0 / IO1 | XTAL32_P / XTAL32_N | 32.768 kHz crystal, 12 pF load capacitors |
| IO2 | LORA_NSS | Strapping pin. The R15 10 k pull-up keeps it high at boot and the radio deselected |
| IO3 | VBAT_ADC | ADC1_CH3. 2.2M/2.2M divider (VBAT/2), always on |
| IO4 | LORA_DIO1 | RTC GPIO, so the C3 can wake on a LoRa packet |
| IO5 | VBUS_DET | Charger present (2.5 V). Deep-sleep wake source |
| IO6 / IO7 | SDA / SCL | 4.7 k pull-ups. SHT40 (U5) on the PCB back side |
| IO8 | LORA_BUSY | Strapping pin, R16 10 k pull-up |
| IO9 | BOOT | PAIR/BOOT button SW1 |
| IO10 | LORA_SCK | |
| IO18 / IO19 | USB D− / D+ | J1 USB-C or J2 magnetic connector, plus the J4 jig. USBLC6 ESD protection. Flashing and logs over native USB |
| IO20 / IO21 | LORA_MISO / LORA_MOSI | These were UART0. TXD (IO21) became MOSI, so the ROM boot log only goes into the radio's input |
| EN | EN | Also drives the SX1262's NRESET (the radio resets with the C3) |

On TH2-W the LoRa nets are unconnected, and the pull-ups keep the strapping pins correct.

## Placement (for the dealer install guide)

Put the node **inside the crop**, not in the shade of a tree or hut and not in the rest area. Shade reads 2–5 °C low in the day, a roof reads high at night, and the readings miss the wet in-canopy hours that drive disease.

| Crop | Sensor height |
|---|---|
| Paddy, wheat, potato, tomato, vegetables | At canopy height (about ⅔ of the crop height). Raise it as the crop grows |
| Maize, sugarcane, cotton | Mid-canopy, 1–1.5 m |
| Orchards and vines | Inside the canopy, about 1.5 m, on the north side |
| Polyhouse | Centre of the house at crop height, away from foggers, pads and the door |

Rules for the spot:
- At least **15–20 m inside the field**. Keep away from bunds, trees, buildings, the pump house and channels.
- Choose a **typical patch** of the field.
- **One node per uniform block** (same crop and sowing date, about 1–2 ha).
- Keep the electronics at least 30 cm above the highest flood level.
- Flag the pole so tractors and sprayers avoid it.

**If WiFi at the spot is weak, use TH2-L.** The **master goes in the shade near the router**, mains-powered. It reports link state only and has no T/RH sensor, because the shade would give misleading numbers.

## Mechanical (v0.2: one PCB in the common AWD/TH box)

See `enclosure/README.md` for renders and the common-vs-TH-only parts list.

- **PCB:** **40 × 60 mm**, 2-layer, the same outline and ledge as the AWD head board. It is clamped by the AWD shells' corner posts.
  - **Top side** (faces the deep back shell): every part, including the **1×AA holder for the LiFePO4 14500** (Keystone 2460-class, 57.5 × 17.4 mm, about 15 mm tall).
  - **Back side** (faces the front shell): **only** the SHT40 + C12, SW1 under the AWD membrane at (5, −14), and D2/D3 under the AWD light window at (30.5, −18.5).
- **Box:** the AWD front and back shells, plus one 10 mm vent with an ePTFE membrane over the SHT40 at (20, −50). The SHT40 sits in a foam-sealed pocket behind it.
- **Charging in the field:** a round magnetic socket in the AWD SMA side hole, wired to J2. USB-C (J1) is a bench/factory option only, because the shell ledge blocks it once the box is closed.
- **Mounting:** the box stands upright inside a stacked-plate shield (OD 180, ring opening 92). It sits on a TH base whose socket takes 32 mm PVC, 25 / 20 mm pipe with sleeves, or bamboo with a hose clamp.

### Battery life with the 14500 (about 600 mAh, about 80 % usable)

| Build / schedule | mAh per day | Days per charge |
|---|---|---|
| TH2-W, measure every 15 min, upload every hour | ~3.5 | ~140 (≈5 months) |
| **TH2-W, measure every 15 min, upload every 3 h** | **~1.6** | **~300 (≈10 months)** |
| TH2-L (LoRa), report every 15 min | ~0.8 | ~600 (≈18 months, capped by self-discharge) |

Default TH2-W to **batched uploads every 2–3 hours**. Send at once only when an alert threshold is crossed.

## Notes before layout

1. **Check the CN3058E pin functions against datasheet DSE-CN3058E.** The pin functions come from the datasheet, but the eSOP-8 exposed-pad size in the footprint (`SOIC-8-1EP_3.9x4.9mm_EP2.41x3.3mm`) must be checked too. Also confirm that **TEMP tied to GND disables the NTC check** (R4 = 0R). If it doesn't, fit an NTC divider (TEMP window 45–80 % of VIN).
2. **Check the RT9080-33GJ5 pinout** (1 VIN, 2 GND, 3 EN, 4 NC, 5 VOUT, the same as AP2112K, which is the symbol used here).
3. **IO2 and IO8 are strapping pins.** Keep the R15/R16 pull-ups, even on TH2-W (no radio fitted).
4. **Keep the crystal close.** Y1 and C10/C11 go within 5 mm of module pins 17/18, with a GND guard and no traces underneath.
5. **VBAT_ADC is always on.** VBAT reaches up to 3.6 V, giving 1.8 V at IO3 with 11 dB attenuation. Take readings with the radio idle, and average 8–16 samples (the source impedance is 1.1 MΩ, buffered by C6).
6. **Low-battery cutoff (firmware):** enter permanent deep sleep below about 3.0 V, and send a "charge soon" alert at about 3.15 V.
7. **LoRa antenna:** AE1 (spring antenna) goes at the top edge of the PCB, away from the AA cell and the WiFi antenna. India's licence-free band is 865–867 MHz. Use an SX1262 or LLCC68 868 MHz module, never a 433 MHz-only one (e.g. Ra-01SC).

## Regenerate

The files are generated from `tools/design.py`, which defines the parts, nets and positions.

```bash
cd products/TH-monitor/hardware/TH2-FieldNode
python3 tools/gen_sch.py          # schematic, TH2.kicad_sym, tools/netlist.json
/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli sch erc TH2-FieldNode.kicad_sch
```

Once the PCB work starts, edit in KiCad and stop regenerating.
