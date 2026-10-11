# ESP32-C3 Super Mini: reference

These are the maker's published drawings, from the [Nologo (WMNologo) getting-started page](https://www.nologo.tech/en/product/esp32/esp32c3SuperMini/esp32C3SuperMini.html#schematic-diagram), saved on 2026-10-11. The ICBbuy and Tenstar schematics are reported to be identical ([sigmdel.ca](https://sigmdel.ca/michel/ha/esp8266/super_mini_esp32c3_en.html)). The board has no designer legend, so a given batch may differ.

| File | What it is |
|---|---|
| `esp32c3schematicdiagram.png` | Schematic |
| `dimension.png` | Dimension drawing: 18.00 × 22.52 mm, rows 15.24 mm apart |
| `esp32c3foot1.png` | Pin map (Arduino "ESP32C3 Dev Module") |
| `esp32c3.png` | Photo of the black and pink versions |

## What the schematic shows, and what it means for the Mini-C3

**Power**
- The USB-C VBUS goes straight to the **5V header pin** (header H2 pin 8). A BAT60J Schottky (PD1) then feeds VSYS, which supplies the LDO and the red PWR LED.
- So the pump and relay current drawn from our 5V pin comes straight from USB and does not pass through the diode.
- Feeding 5 V **into** the 5V pin with USB unplugged also works (the diode then feeds VSYS). Do not connect both at once from different sources.
- The **3.3 V** supply is an ME6211 LDO (SOT23-5, 500 mA), which needs about 4.3 V in. Our 3V3 loads (sensors, pull-ups, LEDs) are small.

**Strapping pins**
- **GPIO2** has a 10 k pull-up (UR15).
- **GPIO8** has a 10 k pull-up (UR10). It also drives the **blue LED**: anode to 3V3 through 5.1 k, so the LED is lit when GPIO8 is low.
- **GPIO9** has a 10 k pull-up (UR6) and the **BOOT** button to GND.
- The Mini-C3 leaves GPIO2, 8 and 9 unconnected, as these pull-ups require.

**Reset:** CHIP_EN has a 10 k pull-up, a 100 nF capacitor and the RST button.

**USB:** native USB (GPIO18/19) with 22 Ω series resistors and 5.1 k CC pull-downs. **UART0** is on GPIO20 (RX) and GPIO21 (TX).
- The Mini-C3's relay is on GPIO20. It is an input at boot, so the relay stays off while the module starts.

**Clock and antenna:** a 40 MHz crystal (3225) and a ceramic chip antenna marked "C3" at the end away from the USB.
- The Mini-C3 keeps copper pour out from under that end (`NO_POUR` in `tools/design.py`).

**Header order, component side up, USB at the top**

| Side | From the USB end |
|---|---|
| Left | IO5, IO6, IO7, IO8, IO9, IO10, IO20, IO21 |
| Right | 5V, GND, 3V3, IO4, IO3, IO2, IO1, IO0 |

This matches `ASC.pretty/ESP32-C3_SuperMini_Socket`.

**Pin rows along the board**
- They are **not centred**. The first row is 1.53 mm from the USB end and the last is 3.18 mm from the antenna end.
- The USB-C receptacle overhangs the USB end by about 1.9 mm.
- The footprint's fab outline and the 3D model use these numbers. On the Mini-C3 the receptacle ends about 1.5 mm beyond the board's top edge.
