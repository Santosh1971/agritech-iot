# Wireless Pump Controller (WPC) — System Specification (v0.3)

**Product:** Wireless Pump Controller (WPC)
**Platform:** Shared hardware platform with FM1 / WM1 (agritech-iot line)
**Radio:** LoRa (SX1262), ESP32 30-pin dev board on both nodes
**Repo:** `~/Projects/agritech-iot/products/WPC-pumpcontroller/` (`docs/`, `firmware/`, `hardware/`, `mobile-app/`)
**Date:** 6 Sep 2026 (rev 3 — first real Pump Node PCB, analog sensing, manual override, configurable TX power)

**Rev 3 supersedes v0.2.** Everything in v0.2 (Master architecture, join flow, level assignment) still holds; this revision documents what changed once the actual Pump Node PCB arrived and went through bring-up: IN1/IN4 became real analog sensing channels (not just digital status), a manual override was added, TX power became field-adjustable, LED wiring was corrected against the schematic, and the Master's polling/pacing model was redesigned. See `WPC_LoRa_Protocol_v0.3.md` for the wire-level and HTTP details, and `WPC_Mobile_App.md` for the Flutter app.

---

## 1. Purpose

WPC maintains the water level of a **centralised sump** that is fed by **N pumps scattered around a field**. A single **Master Node** measures sump level (via float switches) and decides which remote pumps to turn ON/OFF, based on per-pump level assignments configured by the user, or a manual override set by the operator. Each pump is switched by its own **Pump Node**, which talks to the Master over LoRa and also reports two analog sensor channels back.

## 2. System Roles

### 2.1 Master Node
- Reads up to **3 water-level float switch inputs** (IN1–IN3) plus a separate **"No Power" input** (IN4) — unchanged from v0.2.
- Holds the level-to-pump assignment table, **and** a per-pump manual override (auto/manual, ON/OFF) — see §5.
- Sends ON/OFF commands to Pump Nodes over LoRa, paced by a fixed inter-message gap (see §6) rather than a continuous or fixed-heartbeat poll.
- Receives each Pump Node's IN1/IN4 raw ADC telemetry piggybacked on the existing command/ack exchange (no separate telemetry message).
- Hosts a **SoftAP** for local status viewing and configuration (pump states, current level, TX power, override, IN1/IN4 telemetry).
- Accepts new Pump Node registrations via LoRa `JOIN_REQUEST`/`JOIN_ACCEPT` — no app step needed for a pump to first appear.
- Has a unique **Master ID**, derived from the ESP32's MAC address — used during Pump Node pairing so co-located systems don't cross-talk.

### 2.2 Pump Node
- Switches one pump via a **relay dry contact to the pump's existing starter** (not direct motor switching).
- Reads **two analog channels, IN1 and IN4**, in addition to their original digital/boolean reads (see §3.3) — raw ADC values are sent to the Master with every command acknowledgment and are also readable directly from the Pump's own SoftAP.
- Joins a specific Master using that Master's MAC-derived ID, captured during provisioning via its own SoftAP (or via the app's Provision screen).
- Executes ON/OFF commands received from its bound Master; fails safe to **OFF** if it loses contact with the Master for more than 60s.
- Has its own field-adjustable LoRa TX power, independent of the Master's.

### 2.3 App
- **Single common app** (Flutter), used both by the owner/operator (status, level assignment, manual override, general config) and by the field installer (Pump Node → Master assignment, TX power, ADC calibration viewing). See `WPC_Mobile_App.md` for the full screen-by-screen reference.
- The node-assignment (installer) feature is **not currently password-gated** in the app — this remains an open item (§9).

## 3. Hardware

### 3.1 Master Node — unchanged from v0.2
- **Power:** +8V input (J3) → 5V (AMS1117-5.0) → 3.3V (AP2112K-3.3 for LoRa logic).
- **Radio:** SX1262 LoRa module (ISC-SX1262-B) over SPI, DIO1, RESET, BUSY, coax antenna (J2).
- **MCU:** ESP32 30-pin CP2102 dev board.
- **Water Level Inputs (IN1–IN3):** float switch inputs, each with ESD diode + 470Ω series resistor + 10kΩ pull-up + 100pF filter cap, one connector (J4).
- **No Power Input (IN4):** separate connector (J1), same protection topology, used as a fault indicator rather than a level.

### 3.2 Pump Node — as built on the first real PCB (confirmed against schematic, Sep 2026)

The Pump Node's IN1/IN4 protection circuit is electrically identical to the Master's level-switch inputs (ESD diode → 470Ω series resistor → GPIO pin, with a 10kΩ pull-up to 3.3V and a 100pF filter cap at the MCU pin) — it was originally designed for a **digital switch contact**, not a proportional analog sensor. Both channels are nonetheless read as analog (§3.3) by feeding an external voltage into the same connector; because of the pull-up, an unconnected or high-impedance signal will read near full-scale, so any external sense circuit driving these channels must be a real low-impedance source relative to 10kΩ.

**Confirmed pin map** (from the schematic and the ESP32 30-pin dev board's own pin-name symbol — `VP(36)`/`D35` etc.):

| Function | Net (schematic) | GPIO | Notes |
|---|---|---|---|
| IN1 sense | IN1_MCU | 36 (VP, ADC1_CH0, input-only) | analog + digital, see §3.3 |
| IN4 sense | IN4_MCU | 35 (ADC1_CH7, input-only) | analog + digital, see §3.3 |
| IN1 LED | IN1_LED_MCU | 33 | see §4 |
| IN4 LED | IN4_LED_MCU | 13 | see §4 |
| Relay coil driver | RELAY1_MCU | 32 | ULN2003 → cube relay K1 → RL1 dry contact |
| LoRa activity LED | LoRa_LED | 4 | see §4 — **firmware previously had this swapped with Pump-ON, fixed Sep 2026** |
| Pump-ON (relay) LED | Pump_ON_Yellow_LED | 16 (Rx2) | see §4 — **firmware previously drove GPIO4 here by mistake** |
| Onboard WiFi/status LED | (dev-kit onboard) | 2 | |
| LoRa radio | SPI + control | NSS=5, SCK=18, MOSI=23, MISO=19, RESET=25, BUSY=27, DIO1=26 | identical SX1262 front-end to Master |

There is **no IN2/IN3** on the Pump Node — only IN1 and IN4 exist as sense channels. An earlier iteration briefly added a second analog channel on GPIO39 (called IN2, intended for a separate "current sense") before the real PCB confirmed there's no hardware for it; that code was removed.

### 3.3 IN1/IN4 analog sensing (new in this rev)

Both channels are read two ways from the same physical pin:
- **Digital** (`digitalRead`, active-low): the original boolean "pump status"/"no power" reads, still sent in the wire protocol for backward compatibility, not currently surfaced in the app.
- **Analog** (`analogRead`, averaged over 16 samples): raw 0–4095 ADC counts, sent to the Master with every acknowledgment and shown in the app. **Attenuation is set to `ADC_0db`** (full-scale ≈1.1V) rather than the ESP32 default (`ADC_11db`, ≈3.9V full-scale) — the default attenuation is well-documented as non-linear below ~1V, which is exactly the range these two channels operate in during bench testing with a potentiometer. **If the real sense circuit's full-scale output exceeds ~1.1V once running under actual load, this attenuation will need to be raised (or a hardware divider added) — it has only been validated against a bench potentiometer sweeping roughly 0–0.6V, not the final production sensor signal.**

Calibration (raw ADC → real volts/amps) is explicitly deferred — see backlog. Raw values are shown as-is in the app, labeled "raw" rather than implying calibrated units.

**Bring-up note for future readers:** during first-PCB bring-up, both channels appeared permanently pegged near 4095 despite a real, correctly-varying signal measured with a multimeter at the exact GPIO pin. This was eventually traced to the *test rig's* ground connection (a bench potentiometer's common GND not making reliable contact in a terminal block) — not a PCB or firmware defect. Both channels, once confirmed by isolating firmware into a bare ADC-only test sketch and by fixing the ground connection, read correctly. If this symptom reappears in the field, check the sense circuit's ground path before suspecting the board.

## 4. LED Behavior (Pump Node) — corrected/finalized this rev

| LED | GPIO | Behavior |
|---|---|---|
| IN1 status | 33 | Steady ON when IN1's averaged voltage is above 250mV, else OFF. Independent of join state. |
| IN4 status | 13 | Same, 250mV threshold, independent of join state. (No longer a special "no-power fast-blink" — both channels are now treated symmetrically as plain analog indicators.) |
| Pump-ON | 16 | Mirrors the relay's actual commanded state (ON when relay energized). |
| LoRa activity | 4 | Non-blocking blink, same mechanism as the Master's own LoRa LED: **1 blink** when the Pump transmits (`JOIN_REQUEST` or `CMD_ACK`), **2 blinks** when it receives a message addressed to it (a valid `JOIN_ACCEPT` for this pump, or a `LEVEL_CMD` for its assigned slot). Packets that get silently dropped (wrong Master ID, wrong pump/slot, bad CRC) do not blink. |
| WiFi/status | 2 | Slow double-blink-pause = AP up, idle; fast continuous blink = a phone is connected to this Pump's own SoftAP; slow continuous blink = SoftAP failed to start. |

The Master's own LED behavior (WiFi status, LoRa activity/error, per-level status LEDs) is unchanged from v0.2.

## 5. Water Level → Pump Control Logic

Unchanged core logic from v0.2 (level-band assignment with hysteresis/debounce), with one addition:

- **Manual override:** each pump can be switched to manual mode from the app (Status screen). When enabled, the pump's desired ON/OFF state is taken directly from the override setting instead of the level-assignment logic — useful for bring-up testing or an emergency, without touching level assignments. **Override is session-only (not persisted to NVS)** — a Master reboot always comes back in automatic mode, so a pump can't be left silently stuck in a forgotten manual state across a power cycle.
- Fail-safe on comm loss (Pump Node side): unchanged, 60s timeout → force relay OFF, rejoin.

## 6. Master Polling / Pacing Model (redesigned this rev)

v0.2 used a fixed 30-second "heartbeat" timer per pump, re-sending the last command periodically to keep telemetry fresh, on top of immediate sends on real state changes. This rev replaces that with a simpler, **automatically-scaling** model — there is no user-facing "refresh interval" setting:

- The Master services **exactly one pump per pass**, then waits a **fixed 5-second gap** (`INTER_POLL_GAP_MS`) before its next transmission to anyone, regardless of cause.
- **Priority 1:** any pump whose desired state has genuinely changed is serviced immediately, jumping ahead of the round-robin — so reaction to a real level-crossing event doesn't degrade as pump count grows.
- **Priority 2:** otherwise, the next pump due in round-robin order is serviced — this is what refreshes online/offline status and IN1/IN4 telemetry for pumps with nothing new to command.
- A full refresh of **N** known pumps therefore naturally takes **~N × 5 seconds** — a direct, automatic consequence of pump count and the fixed gap, not a separately configured value.
- The same 5-second gap doubles as a `JOIN_REQUEST` listening window, so newly-booting or rejoining pumps get more chances to be heard than the old design's occasional dedicated join window.
- This single mechanism also satisfies the original "5 seconds between simultaneous pump starts" inrush-avoidance requirement as a side effect, since no two Master transmissions are ever closer together than 5s regardless of reason — the old dedicated stagger logic was removed as redundant.
- **The 5-second figure is a placeholder**, chosen as a conservative guess for a 1–2km link where a round trip (including retries) could plausibly take several seconds. It has not been validated against a real long-range test yet (only bench-tested at <1m) — see backlog item 4. It's a single firmware constant (`INTER_POLL_GAP_MS` in `master_node/main.cpp`), easy to revise once real-world range data exists.

## 7. Master Node SoftAP / App connectivity — unchanged from v0.2
Open AP (no password, matching the deferred-security decision), synchronous HTTP server, app connects directly.

## 8. LoRa TX Power (new in this rev)

Both Master and Pump Node had a fixed, compiled-in 14dBm TX power. This is now field-adjustable independently on each node:
- Range: **-9 to +22 dBm**, the SX1262's actual hardware limits (per RadioLib).
- Persisted to NVS, applied live (no reboot).
- **Each node's setting only affects what that radio transmits** — since a LoRa link's range is symmetric only if both ends are tuned, changing range requires adjusting **both** the Master (Status/Assign screen) and the relevant Pump Node (Provision screen, while connected to that pump's own SoftAP) independently.
- Higher power trades battery/duty-cycle headroom for range — this is a call for whoever is tuning the deployed link, not something the firmware second-guesses.

## 9. Remaining Open Items (carried over / updated from v0.2)

1. **Pairing security** — still deferred by design (plaintext MAC-ID matching, open APs).
2. **Installer password/PIN scheme** — still not implemented; the app's Provision screen is currently open to anyone who can reach a Pump's SoftAP.
3. **LoRa link-budget validation at real 1–2km range** — still only bench-tested at <1m. This directly affects whether `INTER_POLL_GAP_MS` (§6) and the poll/RX timeouts in `pollPump()` are sized correctly — revisit both together once real range data exists.
4. **IN1/IN4 calibration** — raw ADC → real volts/amps conversion is not implemented; deferred until the real sense-circuit output range is known under actual pump load (see §3.3's attenuation caveat).
5. **Multi-Master coexistence** — a Pump Node still defaults to one compiled-in Master ID until explicitly provisioned via the app; this hasn't changed since v0.2.

## Appendix

See `docs/master_node_schematic.png` and `docs/pump_node_schematic.png` for the original schematics (Master Node schematic is current; the Pump Node schematic image predates the confirmed pin map in §3.2 above — treat the table in this document as authoritative for GPIO assignments).
