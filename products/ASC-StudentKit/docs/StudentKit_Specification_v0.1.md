# ASC Student Kit (Mini and Mega): System Specification (v0.1, draft)

**Product:** ASC Student Kit, in two variants: **Mini** and **Mega**
**Company:** Agri Sensors and Controls (https://agrisenseandcontrol.in/)
**Platform:** Student Product Studio. See `docs/student-product-studio.md`.
**MCU:** ESP32-S3 module (decided 2026-10-07)
**Date:** 7 Oct 2026, rev 0.1. **This is a draft.** Every item marked *proposed* still needs a decision.

**This kit is Project #0 of the Student Product Studio.** We design our own kits by following the same stages a student will follow: problem → specification → architecture → build → test → enclosure → report. This document is the output of stages 1 and 2, written in the studio's spec format (requirement IDs, plain-language purpose). It is therefore the first test of the studio's templates. Wherever the template turned out awkward while writing this document, the problem is noted in §9.

---

## 1. Problem (stage 1)

BSc Agriculture students need to build a working farm IoT device during a semester. They should not have to design electronics, write code from scratch, or build an enclosure. The devices they build must:
- be safe;
- be reliable enough to run a short field trial;
- be supportable by us across many colleges.

Colleges order **per batch** (one order per cohort). Some projects are simple single-sensor classroom builds, and some are final-year field deployments. One kit cannot serve both at a sensible price, so there are two:

- **Mini**: low-cost and classroom-first. It runs from mains or USB power and connects over WiFi. A typical project has 1–3 sensors and one output.
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

## 3. Proposed feature split (Mini vs Mega)

All rows are *proposed*.

| Feature | Mini | Mega | Notes |
|---|---|---|---|
| MCU | ESP32-S3 module | ESP32-S3 module | Same module on both. Native USB is used for flashing from the browser. |
| USB-C (power + flashing) | ✔ | ✔ | |
| 12 V DC input | ✔ | ✔ (9–24 V) | Reverse-polarity protected |
| LiFePO4 cell + charger | — | ✔ | AWD1 power block |
| Solar input (charge) | — | ✔ | |
| Battery voltage monitor | — | ✔ | |
| Sensor ports **S1…** (analog / digital / 1-Wire, 3-pin) | 3 | 6 | Same connector on both kits (SYS-01) |
| I²C ports (4-pin, Grove/Qwiic-style) | 1 | 2 | For the display, BME280, light sensor and similar |
| RS-485 port (industrial NPK and moisture probes) | — | 1 | With 12 V probe supply |
| Low-voltage relay outputs **OUT1…** | 1 | 2 | Dry contact, low voltage only (SYS-03) |
| 12 V solenoid/valve driver (MOSFET) | — | 1 | |
| WiFi | ✔ | ✔ | |
| LoRa SX1262 (866 MHz) | — | ✔ (slot) | Same module and pin map as WPC and AWD1 |
| GSM/4G modem | — | ✔ (slot) | Same family as PC-gsmpump |
| RTC | — | ✔ | DS1307 or the ESP32-S3 RTC with backup cell, to decide |
| OLED display | via I²C port | via I²C port, plus an onboard header | |
| Status LED, buzzer, PAIR/BOOT button | ✔ | ✔ | |
| Board ID (HW-12) | ✔ | ✔ | |
| **Typical student projects** | Soil-moisture alarm, greenhouse temperature and humidity, tank level indicator, a pump *signal* to a contactor box | Battery-powered field node, remote pump over LoRa, SMS alerts, NPK monitoring, AWD paddy water level | |

## 4. Hardware requirements

| ID | Requirement | Mini | Mega |
|---|---|---|---|
| HW-01 | The ESP32-S3 module has an on-board antenna. The keep-out area at the board edge must not be covered by the enclosure's metal parts. | ✔ | ✔ |
| HW-02 | USB-C connects to the S3's native USB (D−/D+). This is used for flashing over Web Serial and the flasher app, so no USB-UART chip is needed. | ✔ | ✔ |
| HW-03 | Every sensor port has ESD protection, a series resistor, a selectable pull-up, and a 3.3 V supply pin with a resettable fuse. This is the same protection approach as the WPC inputs. | ✔ | ✔ |
| HW-04 | Every sensor port pin connects to an **ADC1** channel, so analog readings still work while WiFi is on. | ✔ | ✔ |
| HW-05 | Relay outputs have a coil driver, a flyback diode and an indicator LED. Contacts are rated for low voltage only, and the silkscreen says "LOW VOLTAGE ONLY". | ✔ | ✔ |
| HW-06 | The 12 V input is protected against reverse polarity and over-voltage (TVS). A buck regulator converts it to 3.3 V. | ✔ | ✔ |
| HW-07 | LiFePO4 charger with a solar/12 V input. The battery voltage is read through a switched divider (the AWD1 VBAT_EN pattern). | — | ✔ |
| HW-08 | RS-485 transceiver with automatic direction control. It has a switched 12 V probe supply, so the probe can be powered off between readings. | — | ✔ |
| HW-09 | LoRa slot: the ISC-SX1262-B footprint with the same SPI and control assignment as WPC and AWD1, and an SMA or u.FL antenna connector. | — | ✔ |
| HW-10 | GSM/4G slot with its own supply rail sized for transmit peaks, and a SIM holder. | — | ✔ |
| HW-11 | Silkscreen prints each port name (S1, I2C-1, OUT1…) large and next to its connector, matching the names in the studio. | ✔ | ✔ |
| HW-12 | **Board ID:** a resistor divider on one ADC pin with a different value on each variant, so the firmware can tell Mini from Mega. It allows future variants. | ✔ | ✔ |
| HW-13 | Test pads for the production tester (`projects/tools/production-tester-cli`). | ✔ | ✔ |
| HW-14 | Total cost of the parts on the board, excluding modules: Mini ≤ ₹ *TBD*, Mega ≤ ₹ *TBD*. | | |

## 5. Software (firmware) requirements

| ID | Requirement |
|---|---|
| SW-01 | **Universal firmware** ("ASC-Studio firmware"): one image containing every block driver (studio §3.3). |
| SW-02 | At boot, the firmware reads the board ID (HW-12) and selects that board's port map. If the configuration uses a port the board does not have, the firmware reports it clearly and does not run that rule. |
| SW-03 | The design configuration is a JSON document stored in NVS. It lists which block is on which port, the thresholds, the rules and the reporting method. It can be written over USB serial or WiFi without reflashing. |
| SW-04 | The rule engine supports at least: *if sensor (above/below) value [between times] then output (on/off) [for N minutes]*, with hysteresis. |
| SW-05 | **Fail-safe:** outputs return to OFF on boot, on a configuration error, and when a sensor fails or is disconnected. |
| SW-06 | Every block has a self-test routine. Running it from the studio returns a pass/fail result plus raw values (studio stage 6). |
| SW-07 | Reporting over WiFi/MQTT on both kits. Mega adds LoRa (to a WPC-style Master) and SMS. |
| SW-08 | OTA update when on WiFi, using the existing `FirmwareBuild` and flasher records. |
| SW-09 | **Code mode:** a readable Arduino sketch can be generated from the configuration and builds for the same board (studio §3.3). |

## 6. Mechanical requirements

| ID | Requirement |
|---|---|
| ME-01 | **Trial:** use a **sample ready-made box**. Buy 2–3 candidate IP65 ABS boxes that are easy to get in India, measure them, choose one, and fit the PCB outline to its inside dimensions and screw bosses. |
| ME-02 | The board mounts on the box's own bosses (no extra standoffs if possible). Connectors face one wall, so cable glands line up on one side. |
| ME-03 | The studio's enclosure step (stage 7) produces a printable drilling template for this box: glands, antenna, and an optional display window. |
| ME-04 | Mini and Mega should fit the **same box** if the Mega layout allows it, so we stock fewer boxes. If not, Mega moves to the next size up in the same box family. |
| ME-05 | Space inside for the LiFePO4 cell (Mega) and the cable loops, with the lid closed. |

## 7. Trial plan (Project #0 through the studio stages)

| Stage | Trial output | Who signs off |
|---|---|---|
| 1. Problem / 2. Spec | This document, with all *proposed* items decided | Santosh (mentor gate) |
| 3. Architecture | Port and pin map for Mini and Mega, block diagram, power budget | Auto check plus review |
| 5. Build | Mini rev A and Mega rev A schematics and PCBs (generator flow like AWD1). Prototype batch from JLC. Universal firmware v0.1. | — |
| 6. Test | Self-test of every starter block on both boards, using the production tester | Auto |
| 7. Enclosure | Sample box chosen. Board fitted. Drilling template made and used on the real box. | Santosh |
| 8. Field trial & report | One Mini and one Mega running a real field rule for 1–2 weeks. Report compiled. | Santosh |

## 8. Open decisions

1. Final feature split (§3): which rows move between Mini and Mega.
2. Number of ports on each kit (3/6 sensor ports, 1/2 relays).
3. RTC choice for Mega.
4. Which candidate sample boxes to buy (ME-01).
5. Target prices for the kits (HW-14) and per-batch pricing for colleges.

## 9. Notes for the studio templates (learned while writing this)

- Spec sections map well onto requirement IDs (SYS/HW/SW/ME). Keep this format for student specs, but students see only the plain-language column. The IDs appear in Engineer's view.
- A "Mini / Mega / Notes" comparison table is the clearest way to make a variant decision. The studio's architecture stage should show a student this table when choosing a kit.
