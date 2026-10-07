# Student Product Studio: plan

**Status:** plan v0.1, 2026-10-07. Nothing is built yet.
**Owner:** Agri Sense and Control (agrisenseandcontrol.in)
**Users:** BSc Agriculture students, with their teachers and mentors.

## 1. Goal

A BSc Agriculture student starts from a farm problem and finishes with a working, enclosed IoT product they can test in the field. They should not need to know KiCad, an IDE, CAD tools or electronics.

The platform hides the tools and still teaches. At every step the student sees what was decided and why. A toggle called *Engineer's view* shows the real output behind each step: the spec document, the schematic, the code and the 3D model.

The approach extends the GPSIOAM 2026 workshop (flasher, student-idea prompts, labs page) into the full product cycle.

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| D1 | **Students do not design PCBs.** We supply a **Student Carrier Board**, designed and tested once by us. Students choose which modules plug into which port. | This removes the hardest and most error-prone step. One tested board means students get repeatable results, it is cheap at volume, and we can support it. |
| D2 | **Everything runs on the existing GigaNodes VPS.** The Next.js app, Postgres and PM2 are already there. Heavy jobs run in a separate PM2 worker process, the same pattern as `bridge/`. | There is no new hosting bill. Section 6 shows how the load stays small. |
| D3 | **Students can order both ways:** (a) order a kit from Agri Sense and Control, or (b) order themselves using a downloaded package for JLC3DP and parts. | This is what was asked for. Path (a) is also a revenue line. |
| D4 | **Teach while hiding the tools.** Every step has a "Why?" card, an *Engineer's view* toggle, and a short check before the gate. | Students learn the engineering ideas without having to learn the tools. |
| D5 | **The AI proposes, fixed rules decide.** Claude drafts the spec and writes the explanations. Rule checks against the block library (pins, power, ports, range) are the final word on what can be built. | A student can never be handed a design that cannot be built. |
| D6 | **No mains on student hardware.** Pumps and other 230 V loads are switched through our certified contactor or WPC box. The carrier board only switches low voltage (12 V relay or driver outputs). | Student safety. |

## 3. Core building blocks

### 3.1 Student Carrier Board (our hardware, rev A)

This is designed in-house using the same generator flow as `products/AWD1-paddy/hardware/AWD-FieldNode/tools/`. Draft feature list, to be finalised:

- **MCU:** ESP32-S3 module. The pin map is fixed and published.
- **Power:** 12 V DC in, USB-C, and an optional LiFePO4 cell with charger (the AWD1 power block). Battery voltage is read on an ADC pin.
- **Sensor ports:** 4 keyed ports (3-pin JST), each able to work as analog, digital or 1-Wire. The port number is all a student needs to know.
- **I²C ports:** 2 Grove/Qwiic-style ports, for the display, BME280, light sensor and RTC.
- **RS-485 port:** for industrial soil NPK and moisture probes.
- **Outputs:** 2 low-voltage relay or MOSFET outputs, plus a 12 V solenoid or valve driver.
- **Radio:** WiFi built in. Optional SX1262 LoRa slot with the same module and pins as WPC and AWD1. Optional 4G/GSM slot, the same as PC-gsmpump.
- **On board:** status LED, buzzer, PAIR/BOOT button, and an RTC footprint.
- **Fit:** sized for **one stock IP65 ABS enclosure**, with mounting holes matching that box.

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
| OLED display, RTC (DS1307) | `ds1307_support.patch`, existing variants |

### 3.3 Universal firmware: no compiling for most students

Students do not get a firmware build. They get **one prebuilt firmware**, "ASC-Studio firmware", that holds every block driver. Each student's design is a **small JSON configuration**: which block is on which port, the thresholds, the rules ("pump ON if moisture < 30 % between 06:00 and 18:00"), and how it reports (WiFi/MQTT, LoRa or SMS).

- The flasher writes the firmware once. After that, only the configuration is sent, over USB serial or WiFi. Changing a design takes seconds and needs no server build.
- The firmware reports to the existing MQTT broker using the topics in `webapp/agrisense-webapp/docs/mqtt-topics.md`, so the student's device shows up in the dashboard like any other product.
- **Code mode**, the advanced option: the studio generates a readable Arduino sketch from the configuration. The student can view it, edit it, and build it on the server (§6). This is how interested students move on to real coding.

## 4. The student journey

The journey has 8 stages. Each stage ends with a **gate**: a short concept check, plus a mentor sign-off where the gate is marked.

| # | Stage | What the student does | What the platform does behind the scenes | Engineer's view shows | Gate |
|---|---|---|---|---|---|
| 1 | **Problem** | Describes the farm problem in their own words (Hindi or English) and picks a crop and field type. | Claude asks follow-up questions: how big is the field, where does the water come from, is there power on site, is there mobile network. | — | Auto |
| 2 | **Specification** | Reads and agrees to a one-page spec written in plain language. | Claude drafts the **system specification**, with **hardware, software and mechanical requirements**, from a fixed template. The rule engine checks it is feasible. | The full spec, with requirement IDs | **Mentor** |
| 3 | **Architecture** | Drags blocks onto the carrier board's ports. Blocks that cannot work there are greyed out, with the reason. | Rule checks: free ports, power budget and battery life estimate, radio range, cost. Draws the block diagram automatically. | Block diagram, pin map, power budget | Auto |
| 4 | **Simulate** (optional) | Tries the design in Wokwi before touching hardware. | Builds the Wokwi diagram from the blocks, the same approach as the GPSIOAM labs. | `diagram.json` | — |
| 5 | **Build** | Plugs the modules into the ports shown on screen. Flashes the board from the browser (Web Serial) or the phone (flasher app). | Writes the universal firmware and then the configuration. | Configuration JSON, generated sketch | Auto |
| 6 | **Test** | Follows the guided test checklist: "dip the probe in water, the reading should go above 70 %". | The device runs each block's self-test and reports over serial or MQTT. Results are ticked off automatically. | Raw readings, test log | Auto |
| 7 | **Enclosure** | Chooses the stock box and places glands and windows on a 3D preview. | Generates a parametric OpenSCAD model: a **printable drilling template (PDF)** for the stock box and an **STL** for a 3D-printed lid or box. | STL/STEP, drilling drawing | **Mentor** |
| 8 | **Field trial & report** | Installs the device and watches the data on the dashboard for N days. | Compiles a **project report** (spec, design, test results, field data and graphs) as a PDF for the college practical assessment. | Report source | **Mentor** |

How *teach while hiding* works in practice:

- Every choice shows a "Why?" card taken from that block's `learn.md`, for example "Why capacitive and not resistive soil moisture? Resistive probes corrode within weeks."
- The concept checks at the gates are 2–3 questions, generated from the student's own design: "Your pump turns on at 30 %. What happens just after rain?"
- *Engineer's view* never needs to be opened to finish the project. It exists for curious students.

## 5. Ordering

**Path A: kit from Agri Sense and Control.** The student or the college places an order in the studio. The kit is a carrier board, the modules from their design (the BOM is generated automatically), and the stock enclosure, optionally pre-drilled. An admin page shows orders as *requested → paid → packed → shipped*. The student's project page shows the same status. Colleges can place one batch order for a whole class.

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
- `StudioDesign`: a versioned block-and-port configuration. It is the JSON that is flashed to the device.
- `StudioOrder`: path (KIT or SELF), items, status, cohort batch.
- `StudioJob`: type, input, status, output path, for the worker.
- A student's flashed device becomes an ordinary `Device` row, so the existing dashboard, MQTT bridge and readings all work unchanged.

## 8. Phases

| Phase | Scope | Done when |
|---|---|---|
| **0: Foundations** | Carrier board rev A (design, prototypes, bring-up). First 8 blocks. Universal firmware with the configuration format. | A board configured by hand from JSON reads 3 sensors and drives an output |
| **1: MVP studio** | Stages 1–3, 5 and 6. Student and teacher roles. Project tracking. Kit orders (path A). | One GPSIOAM batch goes from problem to tested device without writing any code |
| **2: Enclosure & report** | Stage 7 (OpenSCAD worker, drilling template, STL, JLC3DP package, estimate). Stage 8 report PDF. Self-order (path B). | A student's device is enclosed and the college receives the report PDF |
| **3: Grow** | Code mode with server builds. Wokwi simulation (stage 4). Hindi interface. More blocks. Teacher analytics. | Students who want to can move on to real code |
| **Later** | An "advanced board" path: a custom PCB generated to fit a chosen stock enclosure, for final-year projects, using the AWD1 generator flow and kicad-cli. | — |

## 9. Open questions

1. **Brand name:** which spelling is correct, "Agri Sense and Control" (the domain), "Agri Sensors and Controls" (the privacy page) or "Agri Sensor and Controls"? The same name should be used everywhere.
2. **Carrier board MCU:** ESP32-S3 (the same as AWD1, with native USB for browser flashing) or classic ESP32 (`esp32dev`, the same as the workshop labs)?
3. **Stock enclosure:** which IP65 box do we standardise on? This sets the board outline.
4. **Kit pricing**, and whether colleges pay per student or per batch.
5. **VPS specification:** RAM and disk, to confirm the worker fits (§6).
