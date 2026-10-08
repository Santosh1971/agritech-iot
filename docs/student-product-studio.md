# Student Product Studio: plan

**Status:** plan v0.1, 2026-10-07. All nine stages work in the `/studio` web app, with the kit firmware and the ASC Studio phone app (see the web app README).
**Owner:** Agri Sensors and Controls (https://agrisenseandcontrol.in/)
**Users:** BSc Agriculture students, with their teachers and mentors.

## 1. Goal

A BSc Agriculture student starts from a farm problem and finishes with a working, enclosed IoT product they can test in the field. They should not need to know KiCad, an IDE, CAD tools or electronics.

The platform hides the tools and still teaches. At every step the student sees what was decided and why. A toggle called *Engineer's view* shows the real output behind each step: the spec document, the schematic, the code and the 3D model.

The approach extends the GPSIOAM 2026 workshop (flasher, student-idea prompts, labs page) into the full product cycle.

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | **Students do not design PCBs.** We supply the carrier board in two kits, **Mini** and **Mega** (§3.1), designed and tested once by us. Students choose which modules plug into which port. | This removes the hardest and most error-prone step. One tested board means students get repeatable results, it is cheap at volume, and we can support it. |
| D2 | **Everything runs on the existing GigaNodes VPS.** The Next.js app, Postgres and PM2 are already there. Heavy jobs run in a separate PM2 worker process, the same pattern as `bridge/`. | There is no new hosting bill. Section 6 shows how the load stays small. |
| D3 | **Students can order both ways:** (a) order a kit from Agri Sensors and Controls, or (b) order themselves using a downloaded package for JLC3DP and parts. | This is what was asked for. Path (a) is also a revenue line. |
| D4 | **Teach while hiding the tools.** Every step has a "Why?" card, an *Engineer's view* toggle, and a short check before the gate. | Students learn the engineering ideas without having to learn the tools. |
| D5 | **The AI proposes, fixed rules decide.** Claude drafts the spec and writes the explanations. Rule checks against the block library (pins, power, ports, range) are the final word on what can be built. | A student can never be handed a design that cannot be built. |
| D6 | **No mains on student hardware.** Pumps and other 230 V loads are switched through our certified contactor or WPC box. The carrier board only switches low voltage (12 V relay or driver outputs). | Student safety. |

## 3. Core building blocks

### 3.1 Student kits: Mini and Mega (our hardware)

There are **two kits**, both built on an ESP32-S3 carrier board:

- **Mini**: classroom-first and low-cost. USB or 12 V power, WiFi, **Bluetooth LE**, **RTC** (for time-based experiments), **4 sensor ports**, 1 I²C port, **2 low-voltage relays**.
- **Mega**: field-first. Everything in Mini, plus **8 sensor ports**, **4 relays**, LiFePO4 battery and solar power, RS-485, and LoRa and GSM slots.

The remaining details of the split are still to be decided. The draft spec, `products/ASC-StudentKit/docs/StudentKit_Specification_v0.1.md`, holds the feature table and the HW/SW/ME requirements. Two rules apply whatever the final split:

- **A block that works on Mini works on Mega without any change.** Both boards use the same connector and pin order for each port type.
- **One firmware runs on both boards.** A board-ID resistor tells the firmware which board it is running on, and the firmware selects that board's port map.

**The kits are Project #0 of the studio.** We design them by following the same stages students will follow, with a sample ready-made box for the enclosure. This tests the studio's templates on real work before any student uses them.

### 3.2 Block library

A block is one versioned folder in the repo, `common/blocks/<block-id>/`. It contains:

| File | Content |
|---|---|
| `block.yaml` | Name, plain-language description, field use, power draw, cost, allowed ports, JLC/LCSC part number (if used on a board), and enclosure cutout (gland, window or antenna) |
| `driver/` | Firmware driver for the universal firmware (§3.3) |
| `learn.md` | The "Why?" card: what it measures, how, its limits, and how it is installed in a field |
| `test.yaml` | Self-test routine and checklist steps (§4, step 6) |
| `wokwi.json` | Optional simulation part, so students can try it before they have hardware |

Starter set, taken from products we already ship:

| Block | Taken from |
|---|---|
| Capacitive soil moisture, soil temperature (DS18B20), air temperature and humidity (DHT22/SHT) | TH-monitor, workshop labs |
| Water flow (hall sensor) | FG1, FM1 |
| Tank or field water level (ultrasonic and capacitive) | WM1/WM2, AWD1 |
| Pump or valve output (via certified box), 12 V solenoid | WPC, PC-gsmpump |
| LoRa link to a Master | WPC, AWD1 |
| GSM/SMS | PC-gsmpump |
| Battery and solar power | AWD1, XL6009 module |
| RTC | `ds1307_support.patch`, existing variants. The kits use a DS3231-class RTC; see the kit spec. (No OLED: the phone app is the screen.) |

### 3.3 Universal firmware: no compiling for most students

Students do not get a firmware build. They get **one prebuilt firmware**, "ASC-Studio firmware", that holds every block driver. Each student's design is a **small JSON configuration**: which block is on which port, the thresholds, the rules ("pump ON if moisture < 30 % between 06:00 and 18:00"), and how it reports (WiFi/MQTT, LoRa or SMS).

- The flasher writes the firmware once. After that, only the configuration is sent, over USB serial, **Bluetooth from the mobile app** (§3.4), or WiFi. Changing a design takes seconds and needs no server build.
- The firmware reports to the existing MQTT broker using the topics in `webapp/agrisense-webapp/docs/mqtt-topics.md`, so the student's device shows up in the dashboard like any other product.
- **Code mode**, the advanced option: the studio generates a readable Arduino sketch from the configuration. The student can view it, edit it, and build it on the server (§6). This is how interested students move on to real coding.

### 3.4 Mobile app: one app that adapts to each design

The system is not complete without a mobile app. Students do not write an app. They **design their app's screen** in the studio, and one shared Flutter app, the **ASC Studio app**, shows it. This works the same way as the firmware (§3.3): there is one app, and each student's design is data.

- **The device describes itself.** The design configuration (§3.3) also holds an **app layout**: which tiles to show (gauge, graph, on/off switch, status lamp, alert), their labels in Hindi or English, units, icons and colours, plus the product name the student chose. The app reads this layout from the device, or from the server, and builds the screen from it. A soil-moisture alarm and a 4-relay greenhouse controller therefore get different apps from the same install.
- **Three ways to connect.** These are the same kinds of link our product apps use (WPC, FG1), plus Bluetooth:
  - **Bluetooth LE**: on the bench or in the field, with no WiFi or internet. Used to send the configuration (no USB cable needed), to set up WiFi, and to view and control the device locally.
  - **Cloud (MQTT)**: from anywhere, through the existing broker. Supports live readings, history graphs, control and push alerts.
  - **Local WiFi**: the device's own access point, as a fallback. This is the pattern WPC and FG1 already use.
- **Studio features in the app.** Students can scan a QR code on the board to claim it into their project, follow the stage-6 test checklist on the phone, and see their project's stage and mentor comments. Teachers get a read-only view of every device in their class.
- **Teach while hiding.** In Engineer's view the app shows the raw MQTT messages and Bluetooth characteristics, and the layout JSON behind each tile.
- **Build and distribution.** The app has one Play Store listing. It is built in CI in the same way as `wpc-app.yml`. Later (Phase 3), a student can get a **branded APK** with their own app name and icon, built in GitHub Actions, never on the VPS.
- The Kotlin flasher app stays as it is, for USB flashing. The Studio app takes over everything that happens after the first flash.

## 4. The student journey

The journey has 9 stages. Each stage ends with a **gate**: a short concept check, plus a mentor sign-off where the gate is marked.

| # | Stage | What the student does | What the platform does behind the scenes | Engineer's view shows | Gate |
|---|---|---|---|---|---|
| 1 | **Problem** | Describes the farm problem in their own words (Hindi or English) and picks a crop and field type. | Claude asks follow-up questions: how big is the field, where does the water come from, is there power on site, is there mobile network. | — | Auto |
| 2 | **Specification** | Reads and agrees to a one-page spec written in plain language. | Claude drafts the **system specification**, with **hardware, software and mechanical requirements**, from a fixed template. The rule engine checks it is feasible. | The full spec, with requirement IDs | **Mentor** |
| 3 | **Architecture** | Drags blocks onto the carrier board's ports. Blocks that cannot work there are greyed out, with the reason. | Rule checks: free ports, power budget and battery life estimate, radio range, cost. Draws the block diagram automatically. | Block diagram, pin map, power budget | Auto |
| 4 | **Simulate** (optional) | Tries the design in Wokwi before touching hardware. | Builds the Wokwi diagram from the blocks, the same approach as the GPSIOAM labs. | `diagram.json` | — |
| 5 | **Build** | Plugs the modules into the ports shown on screen. Flashes the board from the browser (Web Serial) or the phone (flasher app), then sends the design from the ASC Studio app over Bluetooth. | Writes the universal firmware, then the configuration. | Configuration JSON, generated sketch | Auto |
| 6 | **Test** | Follows the guided test checklist: "dip the probe in water, the reading should go above 70 %". | The device runs each block's self-test and reports over serial or MQTT. Results are ticked off automatically. | Raw readings, test log | Auto |
| 7 | **App** | Designs the phone screen: picks tiles for each sensor and output, names them, sets alerts ("SMS me if the tank is below 20 %"), and chooses the product name and icon. Sees a live preview, then opens the result on the real phone. | Adds the app layout to the design configuration, checks it against the blocks (every tile must point to a real sensor or output), and pushes it to the device over Bluetooth or MQTT. | Layout JSON, MQTT topics, BLE characteristics | Auto |
| 8 | **Enclosure** | Chooses the stock box and places glands and windows on a 3D preview. | Generates a parametric OpenSCAD model: a **printable drilling template (PDF)** for the stock box and an **STL** for a 3D-printed lid or box. | STL/STEP, drilling drawing | **Mentor** |
| 9 | **Field trial & report** | Installs the device and watches the data in the app and on the dashboard for N days. | Compiles a **project report** (spec, design, test results, app screenshots, field data and graphs) as a PDF for the college practical assessment. | Report source | **Mentor** |

How *teach while hiding* works in practice:

- Every choice shows a "Why?" card taken from that block's `learn.md`, for example "Why capacitive and not resistive soil moisture? Resistive probes corrode within weeks."
- The concept checks at the gates are 2–3 questions, generated from the student's own design: "Your pump turns on at 30 %. What happens just after rain?"
- *Engineer's view* never needs to be opened to finish the project. It exists for curious students.

## 5. Ordering

**Path A: kit from Agri Sensors and Controls.** The student or the college places an order in the studio. The kit is a carrier board, the modules from their design (the BOM is generated automatically), and the stock enclosure, optionally pre-drilled. Kits are **priced and ordered per college batch** (decided 2026-10-07): the college places one order for a cohort, and an admin page tracks it as *requested → quoted → paid → packed → shipped*. Each student's project page shows the batch status and which kit is theirs.

**Path B: self-order.**
- **Enclosure:** download a ZIP with the STL and step-by-step JLC3DP instructions (material, colour, quantity), and the drilling-template PDF.
- **Modules:** a BOM with suggested sources.
- **Carrier board:** always comes from us, sold on its own.
- **Price:** the platform shows an *estimated* price for the enclosure. JLC does not offer a public quoting API (to be confirmed), so the estimate comes from their published pricing rules and is marked as an estimate. If we later get API access as a partner, we can switch to live quotes.

## 6. Infrastructure on the existing VPS

```
            Browser / phone
                 │
        Nginx ── Next.js app (PM2)  ── Postgres
                 │   /studio pages, APIs, Claude calls
                 │
                 ├── studio-worker (new PM2 process)
                 │     polls a Postgres job table
                 │     • OpenSCAD  → STL / drilling PDF   (seconds, ~100 MB RAM)
                 │     • PlatformIO → code-mode builds    (rare, ~1 GB RAM, 1 job at a time)
                 │     • report PDF generation
                 │
                 └── bridge (existing) ── Mosquitto  ← student devices report here
```

- **No Redis and no Docker needed.** The job queue is a Postgres table (`StudioJob`) that the worker claims with `SELECT … FOR UPDATE SKIP LOCKED`. The worker is a separate PM2 process, so a slow job never slows down the website.
- **Load stays small because of §3.3.** Most students never trigger a firmware build. Code-mode builds run one at a time and are cached by a hash of the source, so identical sketches are built only once.
- **Claude API:** calls are made from the server only. The API key never reaches the browser. Each student has a daily call limit, and the results of each stage are stored so they are not regenerated.
- **To check before starting:** the VPS RAM and disk. About 2 GB free RAM and 3 GB disk is needed for the PlatformIO ESP32 toolchain plus OpenSCAD.

## 7. Data model (Prisma additions)

- `Role` gets two new values: `STUDENT` and `TEACHER`. Teachers see every project in their class.
- `Institution` and `Cohort`, for example GPSIOAM → AGR 322, 2026–27.
- `StudioProject`: owner (student or team), cohort, title, problem text, current stage.
- `StudioStage`: project, stage number, status, output (JSON: spec, design configuration, test log), gate result, mentor sign-off (who and when).
- `StudioDesign`: a versioned block-and-port configuration plus the app layout (§3.4). It is the JSON that is sent to the device and read by the app.
- `StudioOrder`: path (KIT or SELF), placed by the institution for a cohort (KIT) or by a student (SELF), items aggregated from the cohort's designs, quote, status.
- `StudioJob`: type, input, status, output path, for the worker.
- A student's flashed device becomes an ordinary `Device` row, so the existing dashboard, MQTT bridge and readings all work unchanged.

## 8. Phases

| Phase | Scope | Done when |
|---|---|---|
| **0: Foundations (Project #0)** | Mini and Mega spec decided. Rev A boards fitted to a sample ready-made box, then designed, prototyped and brought up. First 8 blocks. Universal firmware with the configuration format, board-ID detection and BLE. **ASC Studio app v0.1** (BLE + MQTT, screen built from the layout). Each step follows the studio stages (spec in `products/ASC-StudentKit/docs/`). | One Mini and one Mega, each configured from JSON, read their sensors, drive an output, are controlled from the ASC Studio app, and run a 1–2 week field trial |
| **1: MVP studio** | Stages 1–3, 5, 6 and 7 (App). Student and teacher roles. Project tracking. Kit orders (path A). | One GPSIOAM batch goes from problem to tested device without writing any code |
| **2: Enclosure & report** | Stage 8 (OpenSCAD worker, drilling template, STL, JLC3DP package, estimate). Stage 9 report PDF. Self-order (path B). | A student's device is enclosed and the college receives the report PDF |
| **3: Grow** | Code mode with server builds. Wokwi simulation (stage 4). Branded APK per student (built in CI). Hindi interface. More blocks. Teacher analytics. | Students who want to can move on to real code |
| **Later** | An "advanced board" path: a custom PCB generated to fit a chosen stock enclosure, for final-year projects, using the AWD1 generator flow and kicad-cli. | — |

## 9. Decisions log

- 2026-10-07: Company name is **Agri Sensors and Controls** (https://agrisenseandcontrol.in/).
- 2026-10-07: Carrier board MCU is **ESP32-S3**.
- 2026-10-07: Kits are priced and ordered **per college batch**.
- 2026-10-07: There will be **two kits, Mini and Mega**. Their contents are still to be decided. They are designed through the studio's own stages (Project #0), using a sample ready-made box.
- 2026-10-07: **Mini** gets an RTC, Bluetooth and 2 relays. **Mega** gets 4 relays and more sensor ports. Both kits have Bluetooth.
- 2026-10-07: Mini has **4 sensor ports**. Mega keeps its RS-485, valve, LoRa and GSM slots. **BLE only** is accepted. Architecture (stage 3) drafted: `products/ASC-StudentKit/docs/StudentKit_Architecture_v0.1.md` and a checked `hardware/pinmap.json`.
- 2026-10-07: Architecture decisions A1–A6 agreed. **The platform is built before the Mini schematic.** Mini is the first project run through the platform, end to end.
- 2026-10-07: Mock-up approved (https://claude.ai/artifact/W8Roz6MRqduxyadTjj2pXs). Web app build started: cohorts, teacher and student roles, projects, and stages 1–3 (Problem, Specification with mentor sign-off, Architecture with the rule checks).
- 2026-10-07: Stages 4–6 built. One ASC Studio firmware for both kits, built by CI as product ASC_KIT. Students write rules in plain words, flash and send the design from the browser over USB (Web Serial), and run a live test checklist. Bluetooth moves to the App stage.
- 2026-10-07: Stages 7–9 built.
  - Firmware 0.2 adds Bluetooth, with changes allowed only after pressing PAIR, and keeps a field log in flash, so a trial needs no WiFi.
  - The Flutter ASC Studio app builds each student's screen from the layout in their design.
  - Enclosure: a 1:1 drilling template and an OpenSCAD model. The box sizes are placeholders until the sample boxes are measured.
  - Report: built from every stage, printable as a PDF.
  - Still to come: the cloud (WiFi/MQTT) link and an STL export done on the server.
- 2026-10-07: **A mobile app is part of the system** (§3.4): one shared ASC Studio app whose screen is built from each student's design.
- 2026-10-07: First end-to-end run on real hardware (the GPSIOAM workshop kit as a Mini stand-in): stages 1–7 done, design sent over USB, live readings in the studio, pump switched from the phone app over Bluetooth.
- 2026-10-08: **Box chosen:** the 180 × 130 × 100 mm box with a clear lid that WM1 and WPC already use. **Mini** is a half board on the box's upper three bosses, two per 100 × 100 mm panel, like WPC. **Mega** uses the full WM1 board outline and all five bosses. Parts may go on both sides. LEDs show through the lid, labelled on the silkscreen. Details: kit architecture §8.
- 2026-10-08: **Mini rev A schematic drafted**, generated from `products/ASC-StudentKit/hardware/Mini/tools/design.py` (the AWD1 generator flow). It has a TPS54202 12 V→5 V buck, USB-C, an AP7361C LDO, a DS3231MZ RTC, 4 XH sensor ports with clamps, a Grove I2C-1 port, 2 SRD relays with coil LEDs, a buzzer and a test header. Its pins are checked against `pinmap.json`. KiCad ERC is still to be run.
- 2026-10-08: Mini relays changed to the slim **Hongfa HF46F** (1 Form A, so the OUT terminals are 2-way COM/NO). **24 V input maximum** is accepted. **No OLED** in the kits: the block is removed from the studio, the simulator and the firmware, since the phone app is the screen.

## 10. Open questions

1. ~~Mini/Mega feature split and sample box~~: both decided (2026-10-07 and 2026-10-08).
2. **VPS specification:** RAM and disk, to confirm the worker fits (§6).
