# ASC Student Kit (Mini and Mega): System Specification (v0.1, draft)

**Product:** ASC Student Kit, in two variants: **Mini** and **Mega**
**Company:** Agri Sensors and Controls (https://agrisenseandcontrol.in/)
**Platform:** Student Product Studio. See `docs/student-product-studio.md`.
**MCU:** ESP32-S3 module (decided 2026-10-07)
**Date:** 7 Oct 2026, rev 0.1 (updated the same day: RTC, Bluetooth and relay counts decided; mobile app added). **This is a draft.** Every item marked *proposed* still needs a decision.

**This kit is Project #0 of the Student Product Studio.** We design our own kits by following the same stages a student will follow: problem → specification → architecture → build → test → enclosure → report. This document is the output of stages 1 and 2, written in the studio's spec format (requirement IDs, plain-language purpose). It is therefore the first test of the studio's templates. Wherever the template turned out awkward while writing this document, the problem is noted in §9.

---

## 1. Problem (stage 1)

BSc Agriculture students need to build a working farm IoT device during a semester. They should not have to design electronics, write code from scratch, or build an enclosure. The devices they build must:
- be safe;
- be reliable enough to run a short field trial;
- be supportable by us across many colleges.

Colleges order **per batch** (one order per cohort). Some projects are simple single-sensor classroom builds, and some are final-year field deployments. One kit cannot serve both at a sensible price, so there are two:

- **Mini**: low-cost and classroom-first. It runs from a 12 V adapter or USB, connects over WiFi and Bluetooth, and keeps time with an RTC for time-based experiments. A typical project has 1–3 sensors and up to two outputs.
- **Mega**: field-first. It adds battery/solar power, long-range radio (LoRa) and cellular (GSM/4G), industrial probes (RS-485), and more inputs and outputs.

## 2. Guiding rules for both kits

| ID | Requirement |
|---|---|
| SYS-01 | **A block that works on Mini works on Mega without any change.** Each port type has the same connector, pin order and electrical behaviour on both boards. |
| SYS-02 | **One firmware image runs on both boards.** The firmware detects which board it is on (see HW-12) and loads that board's port map. |
| SYS-03 | **No mains voltage on either board** (studio decision D6). Outputs switch only low voltage. A 230 V pump is switched through our certified contactor or WPC box. |
| SYS-04 | Students only ever refer to **port names** (S1, S2, …, I2C-1, OUT1). They never use GPIO numbers. GPIO numbers appear only in Engineer's view. |
| SYS-05 | Each kit fits a **ready-made stock enclosure**. For the trial this is a sample box (§6), and the PCB outline is fitted to that box. |
| SYS-06 | Devices report to the existing MQTT broker using the existing topic scheme (`webapp/agrisense-webapp/docs/mqtt-topics.md`). A student's device appears in the dashboard as a normal `Device`. |
| SYS-07 | **The system includes a mobile app** (§5A). Every kit is used through the shared ASC Studio app over Bluetooth LE, MQTT (cloud) or local WiFi. Students design their app screen in the studio and never write app code. |

## 3. Feature split (Mini vs Mega)

Rows marked ✅ were **decided on 2026-10-07**. All other rows are still *proposed*.

| Feature | Mini | Mega | Notes |
|---|---|---|---|
| MCU | ESP32-S3 module | ESP32-S3 module | Same module on both. Native USB is used for flashing from the browser. |
| USB-C (power + flashing) | ✔ | ✔ | |
| 12 V DC input | ✔ (9–24 V) | ✔ (9–24 V) | Reverse-polarity protected. **24 V maximum** (agreed 2026-10-08). |
| LiFePO4 cell + charger | — | ✔ | New design. AWD1 runs straight from its cell and has no charger. Mega needs an 18650 or larger cell; see the architecture §4. |
| Solar input (charge) | — | ✔ | |
| Battery voltage monitor | — | ✔ | |
| ✅ Sensor ports **S1…** (analog / digital / 1-Wire) | **4** | **8** | Same connector on both kits (SYS-01). 8 on Mega is the ADC1 limit; see HW-04. The architecture proposes a 4-pin connector (GND, 3V3, 5V, SIG). |
| I²C ports (4-pin, Grove/Qwiic-style) | 1 | 2 | For the display, BME280, light sensor and similar |
| ✅ RS-485 port (industrial NPK and moisture probes) | — | 1 | With 12 V probe supply |
| ✅ Low-voltage relay outputs **OUT1…** | **2** | **4** | Dry contact, normally open (1 Form A), low voltage only (SYS-03). Mini uses the slim Hongfa HF46F (agreed 2026-10-08). |
| ✅ 12 V solenoid/valve driver (MOSFET) | — | 1 | |
| WiFi | ✔ | ✔ | |
| ✅ Bluetooth LE | ✔ | ✔ | Built into the ESP32-S3, so it adds no parts cost. The S3 has Bluetooth LE only, not Classic Bluetooth. **BLE only was accepted on 2026-10-07.** |
| ✅ LoRa SX1262 (866 MHz) | — | ✔ (slot) | Same module and pin map as WPC and AWD1 |
| ✅ GSM/4G modem | — | ✔ (slot) | Same family as PC-gsmpump |
| ✅ RTC with backup cell | ✔ | ✔ | Needed on Mini for time-based experiments. Proposed: a **DS3231-class** RTC (3.3 V, about ±2 ppm, roughly a minute a year) on the internal I²C bus, not the DS1307 (5 V, drifts minutes a month). Time is synced from NTP or the phone whenever the device is online. |
| ~~OLED display~~ | — | — | **Dropped on 2026-10-08.** The phone app is the kit's screen, and the board's LEDs show through the clear lid. |
| Status LED, buzzer, PAIR/BOOT button | ✔ | ✔ | |
| Board ID (HW-12) | ✔ | ✔ | |
| **Typical student projects** | Soil-moisture alarm, greenhouse temperature and humidity, tank level indicator, timed irrigation (RTC), pump and fogger *signals* to a contactor box (2 relays) | Battery-powered field node, remote pump over LoRa, SMS alerts, NPK monitoring, AWD paddy water level | |

## 4. Hardware requirements

| ID | Requirement | Mini | Mega |
|---|---|---|---|
| HW-01 | The ESP32-S3 module has an on-board antenna. The keep-out area at the board edge must not be covered by the enclosure's metal parts. | ✔ | ✔ |
| HW-02 | USB-C connects to the S3's native USB (D−/D+). This is used for flashing over Web Serial and the flasher app, so no USB-UART chip is needed. | ✔ | ✔ |
| HW-03 | Every sensor port has ESD protection, a series resistor, a selectable pull-up, and a 3.3 V supply pin with a resettable fuse. This is the same protection approach as the WPC inputs. | ✔ | ✔ |
| HW-04 | Every sensor port pin connects to an **ADC1** channel, so analog readings still work while WiFi or Bluetooth is on. The ESP32-S3 has only **10 ADC1 channels** (GPIO1–10). Mega uses 8 for S1–S8 and 2 for battery and solar/12 V sensing. The board ID (HW-12) is read on an ADC2 pin at boot, before the radio starts. If more analog inputs are needed, an I²C ADC block (ADS1115) adds 4 per I²C port. | ✔ | ✔ |
| HW-05 | Relay outputs (Mini 2, Mega 4) have a coil driver, a flyback diode and an indicator LED. Contacts are rated for low voltage only, and the silkscreen says "LOW VOLTAGE ONLY". | ✔ | ✔ |
| HW-06 | The 12 V input is protected against reverse polarity and over-voltage (TVS). A buck regulator converts it to 3.3 V. | ✔ | ✔ |
| HW-07 | LiFePO4 charger with a solar/12 V input. The battery voltage is read through a switched divider (the AWD1 VBAT_EN pattern). The cell must supply 1–2 A GSM peaks, so an 18650 or larger is needed (the AWD1 14500 is too small). | — | ✔ |
| HW-08 | RS-485 transceiver with automatic direction control. It has a switched 12 V probe supply, so the probe can be powered off between readings. | — | ✔ |
| HW-09 | LoRa slot: the ISC-SX1262-B footprint with the same SPI and control assignment as WPC and AWD1, and an SMA or u.FL antenna connector. | — | ✔ |
| HW-10 | GSM/4G slot with its own supply rail sized for transmit peaks, and a SIM holder. | — | ✔ |
| HW-11 | Silkscreen prints each port name (S1, I2C-1, OUT1…) large and next to its connector, matching the names in the studio. | ✔ | ✔ |
| HW-12 | **Board ID:** a resistor divider on one ADC pin with a different value on each variant, so the firmware can tell Mini from Mega. It allows future variants. | ✔ | ✔ |
| HW-13 | Test pads for the production tester (`projects/tools/production-tester-cli`). | ✔ | ✔ |
| HW-14 | Total cost of the parts on the board, excluding modules: Mini ≤ ₹ *TBD*, Mega ≤ ₹ *TBD*. | | |
| HW-15 | **RTC:** DS3231-class RTC with a CR2032/CR1220 backup cell, on the internal I²C bus (separate from the student I²C ports). Its alarm output is wired to a wake-capable GPIO. | ✔ | ✔ |
| HW-16 | **Mega pin budget is tight:** 8 ports, 4 relays, valve driver, LoRa (7 pins), GSM, RS-485, USB, I²C, LED, buzzer and button. The architecture stage decides whether the relays and LED or buzzer move onto an I²C I/O expander (e.g. TCA9554) to free GPIOs. | — | ✔ |

## 5. Software (firmware) requirements

| ID | Requirement |
|---|---|
| SW-01 | **Universal firmware** ("ASC-Studio firmware"): one image containing every block driver (studio §3.3). |
| SW-02 | At boot, the firmware reads the board ID (HW-12) and selects that board's port map. If the configuration uses a port the board does not have, the firmware reports it clearly and does not run that rule. |
| SW-03 | The design configuration is a JSON document stored in NVS. It lists which block is on which port, the thresholds, the rules and the reporting method. It can be written over USB serial or WiFi without reflashing. |
| SW-04 | The rule engine supports at least: *if sensor (above/below) value [between times] then output (on/off) [for N minutes]*, with hysteresis. |
| SW-05 | **Fail-safe:** outputs return to OFF on boot, on a configuration error, and when a sensor fails or is disconnected. |
| SW-06 | Every block has a self-test routine. Running it from the studio returns a pass/fail result plus raw values (studio stage 6). |
| SW-10 | **Bluetooth LE service:** used to send the configuration (SW-03), set up WiFi, read live values, and control outputs. Pairing requires pressing the PAIR button, so a nearby stranger cannot take control. The same service runs on both kits. |
| SW-11 | **App layout:** the configuration includes the app layout (tiles, labels, units, alerts). The device serves it over BLE and MQTT so the app can build its screen (§5A). |
| SW-12 | **Time:** the RTC is the time source for rules. It is synced from NTP or the phone over BLE, and rules keep running offline. |
| SW-07 | Reporting over WiFi/MQTT on both kits. Mega adds LoRa (to a WPC-style Master) and SMS. |
| SW-08 | OTA update when on WiFi, using the existing `FirmwareBuild` and flasher records. |
| SW-09 | **Code mode:** a readable Arduino sketch can be generated from the configuration and builds for the same board (studio §3.3). |

## 5A. Mobile app requirements

The app is the shared **ASC Studio app** (Flutter), described in `docs/student-product-studio.md` §3.4. It is built the same way as the WPC and FG1 apps (`mqtt_client`, `shared_preferences`), with BLE added.

| ID | Requirement |
|---|---|
| APP-01 | One app for every Mini and Mega design. The screen is built from the app layout in the design configuration (SW-11), so no app code is written per student. |
| APP-02 | **Bluetooth LE link:** find nearby kits, pair (PAIR button, SW-10), send the configuration, set up WiFi, show live values, control outputs. Works with no internet. |
| APP-03 | **Cloud link:** live values, history graphs, control and alerts through the existing MQTT broker and topic scheme (SYS-06). Works from anywhere. |
| APP-04 | **Local WiFi link** to the device's own access point, as a fallback (the WPC/FG1 pattern). |
| APP-05 | Tile types for v0.1: gauge/value, graph, on/off switch (relay), status lamp, alert. Labels in English and Hindi. |
| APP-06 | Claim a kit into a student's project by scanning the QR code on the board. A teacher sees a read-only list of every kit in their cohort. |
| APP-07 | The stage-6 test checklist runs on the phone, with self-test results (SW-06) ticked off automatically. |
| APP-08 | **Fail-safe display:** when the app loses its link, the controls are greyed out and show when the last value was received. The app never shows stale values as if they were live. |
| APP-09 | Engineer's view: raw MQTT messages, BLE characteristics and the layout JSON behind each tile. |
| APP-10 | Distributed through one Play Store listing, built in CI like `wpc-app.yml`. A branded APK per student comes later (studio Phase 3). |

## 6. Mechanical requirements

| ID | Requirement |
|---|---|
| ME-01 | **Decided 2026-10-08:** the **180 × 130 × 100 mm box with a clear lid** that WM1 and WPC already use. The PCB outlines are fitted to its screw bosses (architecture §8). |
| ME-02 | The board mounts on the box's own bosses (no extra standoffs if possible). Connectors face one wall, so cable glands line up on one side. |
| ME-03 | The studio's enclosure step (stage 8) produces a printable drilling template for this box: glands and antenna. The lid is clear, so a display needs no window. |
| ME-04 | **Decided:** Mini and Mega fit the **same box**. Mini is a half board on the upper three bosses; Mega uses the full board and all five. |
| ME-05 | Space inside for the LiFePO4 cell (Mega) and the cable loops, with the lid closed. |

## 7. Trial plan (Project #0 through the studio stages)

| Stage | Trial output | Who signs off |
|---|---|---|
| 1. Problem / 2. Spec | This document, with all *proposed* items decided | Santosh (mentor gate) |
| 3. Architecture | Port and pin map for Mini and Mega, block diagram, power budget | Auto check plus review |
| 5. Build | Mini rev A and Mega rev A schematics and PCBs (generator flow like AWD1). Prototype batch from JLC. Universal firmware v0.1 with BLE. | — |
| 7. App | ASC Studio app v0.1: BLE and MQTT links, screen built from the layout, one sample layout per kit | Santosh |
| 6. Test | Self-test of every starter block on both boards, using the production tester | Auto |
| 8. Enclosure | Sample box chosen. Board fitted. Drilling template made and used on the real box. | Santosh |
| 9. Field trial & report | One Mini and one Mega running a real field rule for 1–2 weeks, controlled from the app. Report compiled. | Santosh |

## 8. Open decisions

1. ~~Feature split~~: decided on 2026-10-07. Mini has 4 sensor ports and 2 relays. Mega has 8 ports and 4 relays and keeps the RS-485, valve, LoRa and GSM slots. Both have the RTC and BLE (BLE only is accepted).
2. ~~Architecture decisions A1–A6~~: all agreed on 2026-10-07 (latching relays on Mega, 4-pin sensor ports, Grove I²C, 18650 cell, 4G Cat-1, the -N8 module).
3. RTC part: DS3231-class proposed (HW-15).
4. ~~Which sample box~~: decided on 2026-10-08 (ME-01).
5. Target prices for the kits (HW-14) and per-batch pricing for colleges.

## 9. Notes for the studio templates (learned while writing this)

- Spec sections map well onto requirement IDs (SYS/HW/SW/ME). Keep this format for student specs, but students see only the plain-language column. The IDs appear in Engineer's view.
- A "Mini / Mega / Notes" comparison table is the clearest way to make a variant decision. The studio's architecture stage should show a student this table when choosing a kit.
