# WPC Master & Pump Node Test Jig (WM1-based) — Specification & Wiring

**Date:** 20 Sep 2026 · **Status (updated):** as-built so far — jig R1–R3 → Master L1–L3, Pump relay → jig FL; Pump IN1/IN4 analog and Master IN4 not yet wired. Draft for review — the fixture below has **not been built or run on a real WM1 board yet**
**Scope:** Factory/bench functional test of one **WPC Master** or one **WPC Pump Node** at a time.
**Builds on (already in repo, do not duplicate):** `testjig/README.md` (what is tested, serial protocol, running), `testjig/firmware/src/main.cpp` (jig fw 0.1.0), `testjig/host/wpc_test.py` + `sim_devices.py`, `WPC_LoRa_Protocol_v0.3.md`, `WPC_Specification_v0.3.md`.

This document adds what those lack: a verified **pin-by-pin wiring plan**, the **analog-stimulus circuit**, the **prerequisite checks** on the WM1 board, the **bring-up order**, and **acceptance criteria**. Every connector/pin below was read from the KiCad PCB files in this repo (`WM1-watermaster/hardware/WaterMaster`, `WPC-pumpcontroller/hardware/{WPC,WPC-panel2x,PumpNode}`), not from memory.

---

## 1. Architecture

```
                         ┌────────── PC: wpc_test.py ──────────┐
                         │  USB hub (powered, ≥2 A)            │
                         └───┬───────────────┬────────────┬────┘
                          USB-A            USB-B
                             │               │
                    ┌────────▼───────┐  ┌────▼──────────────┐
   antenna ─ SMA ── │  JIG  (WM1)    │  │  DUT (Master|Pump) │ ── SMA ─ antenna
                    │  jig fw 0.1.0  │  │  production fw     │
                    │                │  │  v0.4.0 + console  │
                    └─┬──┬───────┬───┘  └──┬──────────┬─────┘
       J12/J13 relays ┘  │ J4    │         │ inputs   │ relay contact
        (Master test) ───┼───────┼─────────►          │ (Pump test)
                         └───────┴─────────────────────►
```

- The DUT runs its **normal production firmware**; only its serial console and `TESTMODE` are test-specific.
- The jig plays the *opposite* device: **Pump emulator** for a Master DUT, **Master emulator** for a Pump DUT.
- Two independent paths are exercised: **wired** (float inputs, analog inputs, relay contact) and **radio** (LoRa join/command/ACK, RSSI).
- One DUT at a time; the two DUT harnesses (§3, §4) can both stay permanently on the fixture — only the one being tested is plugged into a DUT.

## 2. Prerequisites — check these before wiring anything

| # | Check | Why |
|---|---|---|
| P1 | ~~LoRa-populated WM1 needed~~ **Confirmed:** the jig WM1 (a Mini build) has LoRa mounted. | Still verify with `RADIO` at bring-up step 1. |
| P2 | The jig is a **30-pin ESP32 dev kit** in the WM1 socket (per PCB: U5). | Pin numbers below are dev-kit pin numbers = GPIO map in the WM1 spec §2.2. |
| P3 | Two antennas (866 MHz) fitted to jig and DUT **before any TX**. | SX1262 with no antenna can be damaged. |
| P4 | **Power-connector polarity differs between the boards** (see §5). | Reversing 8 V on an unprotected board destroys it. Confirm against the physical silkscreen. |
| P5 | Jig relay contacts (Sanyou SRD, 5 V coil) only ever switch **≤ 3.3 V logic** in this fixture. | No mains/8 V through the jig relays. |

## 2a. Current scope (partial fixture)

Until the remaining wires are added, run with the skip flags. Skipped steps print `[SKIP]` and never count as a pass:

```bash
python wpc_test.py master --jig <JIG> --dut <DUT> --skip-in4   # IN4 / No Power not wired
python wpc_test.py pump   --jig <JIG> --dut <DUT> --skip-adc   # Pump IN1/IN4 analog not wired
```
Not covered until wired: Master IN4; Pump IN1/IN4 analog tracking/isolation and the ADC-in-ACK comparison. Remove the flags when §3 (RL4→IN4) and §4.1 (AOUT filters) are wired.

## 3. Wiring — Master DUT (jig emulates a Pump Node)

The WM1's six relays are **normally-open only, with ONE shared common** (`RLY_COMM`, J12-1; NC pads are unconnected on the PCB). That suits the Master's active-low float inputs perfectly: tie the common to the Master's GND and each relay closes one input to GND.

WM1 relay connectors: **J12** = 1 `RLY_COMM`, 2 `RLY1`, 3 `RLY2`, 4 `RLY3` · **J13** = 1 `RLY4`, 2 `RLY5`, 3 `RLY6` (no common on J13 — use J12-1).
Master input connectors: **J4** = 1 `GND`, 2 `IN1`, 3 `IN2`, 4 `IN3` · **J1 "NoPWR"** = 1 `GND`, 2 `IN4`.

| Jig terminal | Signal | → Master DUT terminal | Meaning |
|---|---|---|---|
| J12-1 | RLY_COMM | **J4-1 GND** (and J1-1 GND) | shared return |
| J12-2 | RL1 (595 QA) | J4-2 **IN1** | Level 1 water present when closed |
| J12-3 | RL2 (QB) | J4-3 **IN2** | Level 2 |
| J12-4 | RL3 (QC) | J4-4 **IN3** | Level 3 |
| J13-1 | RL4 (QD) | J1-2 **IN4** | "No Power" input |
| J13-2, J13-3 | RL5, RL6 | *unused* | leave open |

- Closed contact = input pulled to GND = "water present" at that level (README: a pump assigned to a level runs while that level's switch is **open**).
- Master inputs are 470 Ω + 10 kΩ pull-up + 100 pF + ESD — a relay contact to GND is a clean stimulus; no extra parts needed.
- **Radio:** jig antenna and Master antenna, see §8.
- The jig's relay roles here (RL1–4 = level switches) are **test-jig firmware only** and do not conflict with the fixed WM1 production roles (RL1=pump, RL2=doser, RL3–6=valves).

## 4. Wiring — Pump Node DUT (jig emulates the Master)

Pump Node connectors: **J1** = 1 `GND`, 2 `IN1`, 3 `IN4` · **J4 relay** = 1 `RLY_COMM`, 2 `RL1_CON_1` (a single dry contact, Omron G6A) · **J3 power** (see §5).
WM1 **J1 "Flow" (FL)** = 1 `GND`, 2 `FL` (GPIO36). WM1 input connector **J4** = 1 `GND`, 2 `IN1`, 3 `IN2` (GPIO32), 4 `IN3` (GPIO33).

| Jig terminal | Jig signal | → Pump DUT terminal | Purpose |
|---|---|---|---|
| J4-1 | GND | J1-1 **GND** | **common ground — mandatory, see note** |
| **J1-2 (FL)** | `SENSE` (GPIO36, 10 kΩ pull-up on board) | J4-2 `RL1_CON_1` | senses the Pump's relay contact (**as built**) |
| J1-1 (jig GND) | | J4-1 `RLY_COMM` | other side of the contact |
| J4-3 | `AOUT1` (GPIO32 PWM) → RC filter | J1-2 **IN1** | analog stimulus, channel 1 |
| J4-4 | `AOUT2` (GPIO33 PWM) → RC filter | J1-3 **IN4** | analog stimulus, channel 4 |

- **Ground:** a poor ground in a *bench* sense circuit already cost a full debugging session on the first Pump PCB (spec §3.3 bring-up note). Use a ferrule-crimped wire, not a breadboard or a shared terminal.
- **Relay sense (as built):** Pump relay ON ⇒ FL = 0. Contact closed ⇒ `SENSE` reads LOW (`@OK contact=1`). Polarity of the contact does not matter.

### 4.1 Analog stimulus circuit (this is the one part that needs parts, and the one to tune)

The WM1 has **no DAC** (GPIO25/26 are LoRa), so the jig makes the voltage with **PWM + RC filter**.

What is already on the WM1 for IN2/IN3: ESD diode at the connector → **470 Ω series (R4/R3)** → MCU pin with **100 nF to GND** (C2/C1) and **no pull-up**. The PWM pin sits on the MCU side of that 470 Ω, so the 470 Ω is the filter resistor and only the capacitor is missing:

```
 GPIO32 ──┬── 470 Ω (WM1 R4) ──┬── J4-3 ─────────────┬────────► Pump J1-2 (IN1)
          │                    │                     │
        100 nF (C2, on board)  │                  10 µF  ← ADD (ceramic X7R ≥10 V,
          │                    │                     │      soldered across J4-3 ↔ J4-1)
         GND                  ESD                   GND
```
Same for GPIO33 → J4-4 → Pump IN4. **Add one 10 µF capacitor from each of J4-3 and J4-4 to J4-1 (GND).**

Expected behaviour (calculated, **assuming the Pump's pull-up is at the MCU node like the WM1's and Master's — confirm on the Pump schematic**):

| PWM duty (of 255) | Jig output | Pump MCU pin ≈ | 
|---|---|---|
| 0 | 0 V | 0.28 V (the Pump's own 10 kΩ pull-up shows through 940 Ω) |
| 15 | 0.19 V | 0.46 V |
| 30 | 0.39 V | 0.64 V |
| 45 | 0.58 V | 0.82 V |

- Pump ADC is `ADC_0db` (≈1.1 V full-scale), so the default sweep `--adc-steps 0,15,30,45` covers ~0.5 V of the range with margin below saturation; expected rise ≈ 530 mV vs the pass threshold `--adc-min-span 150`.
- Filter time constant ≈ 470 Ω × 10 µF ≈ 5 ms; PWM ripple at 20 kHz is a few mV. The host should wait **≥100 ms** after each `AOUT` before reading.
- Isolation test: drive AOUT1 through its steps while AOUT2 stays at 0 (and vice versa); the other channel must stay within `--adc-crosstalk` (80 mV).
- Numbers are a starting point. **Measure with a multimeter at J4-3/J4-4 during bring-up (§9, step 3) before trusting them, and tune the sweep and thresholds on a golden Pump.**

## 5. Power

| Item | Plan | Note |
|---|---|---|
| **Jig** | USB from the **powered** hub | Relay coils run from the dev-kit 5 V rail through polyfuse F2 (`+5V` → `+5VR`); 4 coils + ESP32 + LoRa ≈ 0.5–0.7 A. Do not use an unpowered laptop port. |
| **DUT — default (D1)** | **USB only** (the same cable as the console) | Simplest; the DUT's +8 V stage is *already declared out of scope* (README "Not covered"). |
| DUT — optional | +8 V on J3 from a bench supply | See polarity warning. Do this **only if** you want the 8 V→5 V stage exercised, and first measure that the dev-kit 5 V/USB and AMS1117 5 V rails don't fight. I could not determine from the schematics whether the dev kit blocks back-feed. |

**⚠ J3 polarity is reversed between the boards (from the PCB files):**

| Board | J3 pin 1 | J3 pin 2 |
|---|---|---|
| WPC **Master** (WPC.kicad_pcb / panel2x) | **+8 V** | GND |
| WPC **Pump Node** (PumpNodeNew.kicad_pcb) | GND | **+8 V** |

Label the fixture leads and check with a meter before connecting. (Also: the Pump J3 is a Metz 2-pin terminal, the Master's a Phoenix MSTBA 2-pin — different connectors.)

## 6. Serial / USB

- One powered USB hub: jig + DUT (+ nothing else on the console path).
- macOS **resets both boards when the port is opened**; the host waits for `ID` from both. A fresh Pump join also clears that slot's override → allow ~30 s after opening ports before the first real step (already handled by the script's wait, but do not shorten it).
- Ports renumber whenever anything is replugged. **Today the host takes explicit `--jig/--dut` ports.** Recommended v1.1: auto-assign by the `ID` reply (jig answers with its firmware string; DUT with MAC/ID) so an operator cannot swap them.
- The console occasionally drops/garbles a line (~1 in 100 queries); the host already reassembles and retries once (never `REBOOT`/`FACTORYRESET`). Keep that logic.

## 7. What the test proves (map to fixture)

| Test | Fixture path | Source |
|---|---|---|
| Master: IN1–IN4 read correctly & isolated | jig RL1–RL4 → Master inputs | README Master 1 |
| Master: join, level→command, override, ADC-in-ACK, offline, NVS | LoRa (jig Pump emulator) | README Master 2–8 |
| Master: SoftAP `/status`; cloud round-trip (optional) | jig WiFi / office WiFi + MQTT | README Master 9–10 |
| Pump: relay closes/opens contact | Pump relay ↔ jig `SENSE` | README Pump 2 |
| Pump: IN1/IN4 analog tracking & isolation | jig `AOUT1/2` → Pump IN1/IN4 | README Pump 3 |
| Pump: join, ON/OFF, ACK, RSSI, fail-safe | LoRa (jig Master emulator) | README Pump 4–6 |
| Pump LEDs | operator, `--visual` | README Pump 1 |

Not covered (unchanged): LED colour/brightness, antenna quality beyond RSSI, the Master's +8 V power stage.

## 8. RF arrangement

- **Recommended (v1):** both whip antennas on the fixture, **~1 m apart, clear of metal**, jig and DUT antennas vertical. RSSI limit `--min-rssi` starts at −90 dBm and is **set from measurements on a golden unit**, then tightened.
- **Alternative (if the factory has other WPC units transmitting nearby):** coax from each SMA to a **30 dB inline attenuator** in a shielded cable, or a small metal test box. This stops a neighbour's Master/Pump joining the jig, and stops the jig hitting neighbours.
- Radio sync word is derived from the Master ID (`syncWordFor`): give each jig its **own emulated Master ID** so two jigs on one bench cannot cross-talk.
- **Open item from earlier bench work:** Master↔Pump RSSI was only −106 dBm at 0.5 m while a defective ESP32 was in the system, and the cause was never confirmed. On first jig bring-up record `LORASTAT lastRssi` for a known-good pair; if it is still that low, the cause is RF, not the jig.

## 9. Bring-up order (do in this order; stop at the first failure)

| Step | Action | Pass |
|---|---|---|
| 1 | Flash jig fw on the WM1 (`pio run -t upload --upload-port …`); send `ID`, `RADIO` | replies `@OK`; radio initialised (confirms **P1**) |
| 2 | **Relays, no DUT:** `RELAY 1 1` … `RELAY 4 1` with a meter across J12-1 ↔ J12-n; also `RELAYS 63` then `RELAYS 0` | each closes/opens only its own contact; bit order matches (RL1 = QA). *The 74HC595 driver copies the WM1 shift order but has never been exercised here.* |
| 3 | **Analog, no DUT:** fit the 10 µF caps; meter on J4-3/J4-4 vs J4-1; `AOUT 1 0/15/30/45` | ≈0/0.19/0.39/0.58 V (no DUT load), stable, no visible ripple |
| 4 | **Sense, no DUT:** short J1-2 (FL) to J1-1; `SENSE` | `contact=1`; open ⇒ `contact=0` |
| 5 | **Master test** with a *known-good* Master: `python wpc_test.py master --jig … --dut …` | all Master steps pass; review timings |
| 6 | **Pump test** with a *known-good* Pump: `… pump … --visual` | all Pump steps pass; **record the ADC readings and RSSI per step** |
| 7 | Tune `--adc-steps`, `--adc-min-span`, `--min-rssi` from step 6 readings | thresholds written into the script defaults / a config file |
| 8 | **Fault injection:** break each thing deliberately (unplug IN2, unplug relay wire, disconnect antenna, wrong ground) | each yields a specific FAIL, never a PASS |
| 9 | **Repeatability:** 20 consecutive runs each on a golden Master and Pump | 20/20 PASS, no flaky step |
| 10 | Two-jig / two-DUT check with different emulated Master IDs | no cross-talk |

Do steps 2–4 with a meter before ever connecting a DUT: the failure mode you are protecting against is the fixture, not the DUT.

## 10. Acceptance criteria

1. Every "verified" test in §7 passes on a golden Master and a golden Pump, 20/20 consecutive runs.
2. Every fault injected in step 8 is detected and reported as FAIL with a step name.
3. A run that crashes, times out or loses a serial port is recorded as **FAIL**, never blank or PASS (already implemented — verify it on hardware).
4. Both DUTs are left in factory state (`FACTORYRESET`) at the end, and the CSV row carries the DUT MAC and firmware version.
5. Cycle time per DUT is **measured** in step 9 and recorded here (target to be set once known; waits in the script — join 30 s, offline 30 s, reboot — dominate).

## 11. Fixture parts

| Qty | Item | Notes |
|---|---|---|
| 1 | WM1 board, **LoRa-populated**, ESP32 dev kit | jig |
| 1 | Powered USB hub, ≥2 A, + 3 short USB cables | |
| 2 | 866 MHz antennas (SMA) | jig + DUT |
| 2 | 10 µF, ≥10 V X7R ceramic | analog filter, at J4-3 and J4-4 |
| 1 | Master DUT harness: 6 wires with ferrules → J12/J13 relay terminals to Master J4 + J1 | §3 |
| 1 | Pump DUT harness: 5 wires with ferrules → WM1 J4 to Pump J1 + J4 | §4 |
| — | Labels: "MASTER J3: +8V is pin 1" / "PUMP J3: +8V is pin 2" | §5 |
| opt | Bench 8 V supply; 2× 30 dB attenuators + coax | only if D1/§8 alternatives are chosen |

## 12. Open items and known risks

| # | Item | Impact |
|---|---|---|
| R1 | Jig fw and fixture have **never run on a real WM1** | expect to tune thresholds; bring-up §9 |
| R2 | Pump's IN1/IN4 pull-up location assumed at the MCU node (§4.1) | changes the 0 V offset (0.28 V) not the method |
| R3 | DUT +8 V path vs USB back-feed unknown (§5) | decides whether 8 V power is added (D1) |
| R4 | Pump 60 s fail-safe vs Master round-robin (~5.5 s × N pumps) trips at ~10+ pumps | firmware issue, not jig; unaffected because the jig tests single-pump |
| R5 | Two ESP32 dev boards previously had a dead reset/auto-flash circuit | test any spare with `esptool chip-id` standalone before it goes into the jig or a DUT |
| R6 | Host takes explicit ports | v1.1: auto-detect by `ID` |

## 13. Decisions needed from you

- **D1 — DUT power:** USB only (my recommendation; matches "8 V stage not covered") or add a switched 8 V supply?
- **D2 — RF:** open-air 1 m (recommended to start) or attenuated/shielded?
- **D3 — Fixture form:** loose wires + ferrules for bring-up (recommended), then a small carrier board with two labelled DUT connectors once thresholds are fixed?

## 14. First real-hardware run (20 Sep 2026, Master DUT, `--skip-in4`)

Jig = WM1 MAC 209BA9690BE8, jig fw 0.1.0 (SENSE on GPIO36). DUT = Master 68A99B20.

| Result | Step | Finding |
|---|---|---|
| PASS | IN1–IN3 via jig RL1–RL3, join, registered in table, level 1–3 logic, override/AUTO, NVS reboot | fixture wiring and jig relay/radio paths work |
| PASS (after fix) | ADC in CMD_ACK reaches Master | first run failed because of interference, next row |
| FAIL → cause found | ADC / offline detection | **A second, already-joined Pump Node on the bench keeps answering slot 0** after the Master's `FACTORYRESET` (Master table ADC showed 4095 = the real Pump's floating inputs; `PUMPEMU NOACK 1` had no effect). **Rule: for a Master test, the real Pump must be powered off or held in reset** (open its port with RTS asserted, or unplug). Add a host-side check (e.g. `PUMPS` must show only the jig's pumpId) in a later version. |
| FAIL | Master TX RSSI at jig | −108 dBm (SNR ≈5) vs −90 limit. Same weakness as earlier bench pairs (−106 at 0.5 m). Link still decodes (SF9), so this is RF level, not logic. Antennas/separation to be checked; do **not** loosen the limit to hide it. |
| FAIL | pump comes back online after ACKs resume | **Real Master-firmware finding, reproduced on hardware:** after a pump is marked offline the Master polls it with a 200 ms receive window (`rxTimeout` in `master_node/src/main.cpp`, single attempt). A 17-byte ACK at SF9/125 kHz takes ≈200 ms on air, so it lands outside the window. Jig log: `acks` climbed 11→23 while the Master logged "RX interrupt never fired this window" and the pump stayed offline. Recovery then depends on the Pump's 60 s fail-safe + rejoin. Not yet repeated with a real Pump; **Fixed 20 Sep:** offline pumps now use `POLL_TIMEOUT_MS` (500 ms), still a single attempt; re-run shows offline=True recovered=True. |

### 14.1 Pump run notes (20 Sep 2026)

- **Pump relay console step ordering (fixed in `wpc_test.py`):** Pump fw ≥ 0.4.0 forces its relay OFF whenever it is unjoined (fail-safe), so a console `RELAY 1` only holds while joined. The direct-drive relay check now runs *after* the join, each state held 0.6 s.
- **Relay sense pin:** FL/GPIO36 was tried and rejected (floating, picks up 50 Hz mains; FL input suspected damaged). Sense is on **IN1/NP, GPIO14** with the pull-up; wiring to Pump J4 (`RLY_COMM`→jig GND, `RL1_CON_1`→jig IN1) still to be confirmed — `SENSE` read open while the relay was acknowledged ON.
- **Pump with no antenna:** RSSI at the jig −91 dBm (limit −90); expect PASS once an antenna is fitted, or run with `--min-rssi -95` until then.

### 14.2 Pump test result (20 Sep 2026): PASS

After fixing a broken wire between the Pump relay and the jig (Pump J4 `RLY_COMM`→jig GND, `RL1_CON_1`→jig IN1/GPIO14, continuity verified with a meter): `wpc_test.py pump --skip-adc --min-rssi -95`, Master held in reset → **9 pass, 0 fail, 2 skipped** (LED check, IN1/IN4 analog). Pump→jig RSSI −84/−85 dBm with **no antenna on the Pump** (would also pass the default −90). Still to do: Pump IN1/IN4 analog wiring, Master IN4, LED check (`--visual`), Pump antenna.

## 15. Production Station UI (20 Sep 2026)

`testjig/host/wpc_station.py` + `wpc_station.html` (Flask, like the FG1 Flash Bridge). See `testjig/README.md` for use.

| Requirement | Implementation |
|---|---|
| Flash Master and Pump, any number incl. 0 | Scan finds ports; per-port role (Master / Pump / Jig / unused). PlatformIO upload per unit with retries; real exit status checked; then the board must boot answering as the chosen role. |
| Test | `wpc_test.py` per unit, other units held in reset; operator prompt to swap the fixture harness before each unit (the jig wiring is physically per-unit). |
| Customer pairing (1 Master + n Pumps), ship as-is | `MASTER <id>` sent to each Pump; waits for `joined=1` on the Pump and `online=1` in the Master's `PUMPS`; optional `FORGETALL` first; `TESTMODE 0`; **no** `FACTORYRESET`; logged to `wpc_pairings.csv`. |
| Antenna test with a setting | Dedicated `ANTENNA:` step, average of N packets, limit per unit type, enforce or record-only. |

Verified on the real bench (jig + Master 68A99B20 + Pump 2368): scan identifies all three; production run of both units PASS; pairing PASS; pairing survived a Pump flash and reboot (Master `PUMPS` online, Pump `targetMaster=68A99B20`).

Not covered by the UI yet: Pump LED check (`--visual` needs a terminal prompt), the SoftAP/cloud tests, flashing the Jig itself, several Pumps paired at once (code supports it; only 1 Pump was available to test), and fixture positions for more than one DUT at a time.
