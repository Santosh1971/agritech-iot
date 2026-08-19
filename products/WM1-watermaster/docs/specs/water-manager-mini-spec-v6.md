**Change log from v5:** IN1 polarity corrected — HIGH is power OK, LOW is No Power (was backwards). Outage-delay/queuing behavior for back-to-back schedules spelled out. Pump strictly requires at least one open valve — no pump-only mode, auto-off when the last valve closes. Access model simplified to a single app for Farmer/Dealer/Admin (no separate web console for v1). Added an explicit "what's actually being built" breakdown (firmware + backend/database + mobile app).

# Water Manager-Mini — Firmware & Mobile App Specification (v6)

**Product line:** Agri Sensors and Controls — Water Manager family
**This variant:** Water Manager-Mini (WiFi/Internet only — no LoRa)
**First deployment:** Kamta (v1 mobile app, dealer role)
**Power source:** 8V solar-charged battery
**Change log from v1:** RTC changed to DS1307; GPIO table corrected against the zoomed ESP32 pinout; IN1 ("No Power") behavior defined; IN2/IN3 shown as raw voltage only for now; extension relay board deferred; broker strategy clarified; mobile app section rewritten around the NB Agri Automation layout supplied for Kamta, with a feasibility call-out per page; added multi-customer admin/diagnostic view; added 3–6 month history/retention plan with dummy data for app preview.
**Change log from v3:** Sequencing confirmed as unrestricted — pump (auto) + optional dosing + any combination of the 4 valves, no firmware-enforced simultaneity limit. Dosing duration confirmed as short relative to the irrigation run (e.g., ~10 min within a 3-hour cycle). Overnight/cross-midnight schedules (e.g., 10 PM–2 AM) now explicitly supported. Pause/resume behavior reconfirmed, including mid-dosing.
**Change log from v4:** Simultaneous-valve decision reframed — no firmware limit, but pressure-threshold alert (enable/disable, farmer-set) gives the farmer the information to choose their own safe valve combination. Scheduling model changed from weekday-based to interval-based (daily / alternate-day / every-N-days), plus an optional sequence-rotation mode — calendar days aren't how farmers think about this. Dashboard now shows per-zone water/duration + fertigation applied. Manual override explicitly covers pump and dosing, not just valves. Dealer role (Kamta) upgraded from view-only diagnostics to full control on assigned devices, since he supports customers directly.

---

## 1. Overview & Scope
Unchanged from v1: standalone ESP32 controller, up to 6 relay-driven valve zones (extension board deferred — see §7), flow + dual 4-20mA pressure sensing, RTC-scheduled irrigation independent of connectivity, cloud + local control, solar/battery power budget.

---

## 2. Hardware Reference (corrected against zoomed pinout)

### 2.1 RTC
**Changed:** RTC is **DS1307**, not DS3231.
Implication for firmware: DS1307 has no built-in alarm registers (unlike DS3231's Alarm1/Alarm2 + interrupt pin), so schedule triggering must be done by **firmware polling the RTC time** against stored schedules (e.g., once per minute) rather than relying on an RTC-generated interrupt. This is a minor firmware design point, not a blocker — flagging it so it's designed in from the start rather than discovered later. DS1307 also has no temperature-compensated oscillator (DS3231 does), so expect slightly more drift over time; fine for irrigation-scheduling accuracy, and firmware should still re-sync RTC from NTP/cloud whenever online.

### 2.2 ESP32 GPIO Map (from zoomed pinout, dev-kit pin numbers shown)

| Dev-kit pin | GPIO | Net | Use on Water Manager-**Mini** |
|---|---|---|---|
| 16 | EN | — | Not connected |
| 17 | GPIO36 (VP) | Flow_MCU | Flow sensor pulse input |
| 18 | GPIO39 (VN) | Press_IN1_MCU | Pressure sensor 1 (4-20mA) |
| 19 | GPIO34 | Press_IN2_MCU | Pressure sensor 2 (4-20mA) |
| 20 | GPIO35 | Batt_Mon | Battery voltage sense |
| 21 | GPIO32 | IN2 | Generic analog input — raw voltage only for now |
| 22 | GPIO33 | IN3 | Generic analog input — raw voltage only for now |
| 23 | GPIO25 | RESET | LoRa reset — **Pro only, unused on Mini** |
| 24 | GPIO26 | DIO1 | LoRa — **Pro only, unused on Mini** |
| 25 | GPIO27 | BUSY | LoRa — **Pro only, unused on Mini** |
| 26 | GPIO14 | IN1 | "No Power" sense input (see §3.7) |
| 27 | GPIO12 (boot-sensitive) | — | Not connected (left free deliberately — boot-strapping pin) |
| 28 | GPIO13 | RLY_LATCH | 74HC595 latch |
| 29 | GND | — | |
| 30 | VIN/5V_Power_IN | — | +5V in |
| 15 | GPIO23 | MOSI | LoRa — **Pro only** |
| 14 | GPIO22 | SCL | I2C — DS1307 RTC |
| 13 | Tx0/USB | — | Not connected |
| 12 | Rx0/USB | — | Not connected |
| 11 | GPIO21 | SDA | I2C — DS1307 RTC |
| 10 | GPIO19 | MISO | LoRa — **Pro only** (tied to 3.3V on Mini) |
| 9 | GPIO18 | SCK | LoRa — **Pro only** |
| 8 | GPIO5 (boot-sensitive) | NSS | LoRa — **Pro only** (pulled to 3.3V via R13 10K) |
| 7 | GPIO17 | RLY_DATA | 74HC595 serial data |
| 6 | GPIO16 | RLY_CLK | 74HC595 clock |
| 5 | GPIO4 | LoRa_LED | **Pro only** |
| 4 | GPIO2 (boot-sensitive) | OnBoardLED | Not connected — onboard dev-kit LED used directly for WiFi/power status |
| 3 | GPIO15 (boot-sensitive) | — | Not connected |
| 2 | GND | — | |
| 1 | 3V3_Out | — | +3.3V out |

On the Mini build, the LoRa-only pins (23–25 left side; 15, 10, 9, 8, 5 right side) are simply unused — firmware doesn't touch them, and they're free for a future feature if ever needed on this exact board revision.

### 2.3 Relay Allocation (fixed)
Of the 6 relay outputs (RL1–RL6, via 74HC595 + ULN2003):

| Relay | Function |
|---|---|
| RL1 | Main pump |
| RL2 | Dosing (fertigation) pump — single channel, time-based only |
| RL3–RL6 | Irrigation valves — **4 zones available** |

This resolves the earlier "pump-as-separate-relay" open question: yes, one relay is dedicated to the main pump. The flow sensor measures the **main irrigation line only** — there's no flow measurement on the dosing line, consistent with dosing being time-based rather than volume-based.

---

## 3. Firmware Specification

### 3.1 Sensing
- **Flow, Pressure 1/2, Battery:** unchanged from v1.
- **IN2 / IN3:** confirmed generic and undefined for now — firmware reads and reports **raw voltage only** (with a sensible ADC-to-volts conversion), no calibration/scaling UI until the actual sensor is decided. App shows these as plain voltage readouts.

### 3.2 Valve / Pump / Dosing Control (updated)
- **RL1 (pump):** strictly follows valve state — **at least one of RL3–RL6 must be ON for the pump to be ON**, no exceptions. There's no "pump-only" mode and no schedule can run without at least one valve open. The moment the last active valve turns off — whether the schedule finishes normally, the farmer manually closes it, or it's the last valve in a manual-override session — the pump turns off immediately too, automatically, without needing a separate command.
- **RL3–RL6 (4 irrigation zones):** as in v1 — manual on/off, scheduled, time- or volume-limited (volume via the main flow sensor/totalizer).
- **RL2 (dosing):** simple timed switch — on for a configured duration, no flow/volume measurement involved. Firmware supports triggering the dosing relay at a configurable point relative to the irrigation sequence: **start**, **mid**, or **end** of the sequence's run. Only one dosing event and one channel — no per-fertilizer (A/B/C) breakdown, no pre/post/mixed dosing modes. Dosing runs are short relative to the irrigation cycle (e.g., ~10 minutes within a 3-hour run) — no minimum/maximum enforced in firmware, but the app's input field should default toward that kind of range rather than implying dosing runs as long as irrigation does.
- Fail-safe (confirmed): on power loss or reset, **all relays default OFF** (de-energized) — pump, dosing, and all 4 valves fail to the not-running state.
- **Sequencing is unrestricted:** a sequence is simply pump (automatic) + an optional dosing event + any combination of the 4 valves (one, several, or all at once). Firmware doesn't enforce a simultaneous-valve limit — the system will happily run all 4 together if commanded. Instead, the **pressure sensors give the farmer the information to decide for themselves**: a per-channel low-pressure threshold (farmer-configurable, alert enable/disable) flags when a combination is dropping pressure too far — e.g., "pressure below X while 3 valves are open." The farmer uses that feedback to settle on the max combination that keeps pressure healthy for their pump/pipe; the controller informs, it doesn't block.

### 3.3–3.6
Unchanged from v1 apart from two explicit clarifications below (scheduling, connectivity, data logging, power management) — see §5 for the scheduling-engine scope, and §8 for retention.

**3.3a — Overnight / cross-midnight schedules:** confirmed as a real requirement (e.g., a cycle running 10 PM–2 AM the next day). The scheduler works off a start time + duration (or start + volume target), not a fixed same-day start/end window, so a run is allowed to cross midnight without needing to be split into two schedules. A schedule's "day" (for repeat-day matching, e.g., "runs Tue/Thu/Sat") is anchored to its **start** day — a Tuesday 10 PM schedule that finishes at 2 AM Wednesday is still "Tuesday's cycle," and the farmer only ticks Tuesday in the repeat-days selector, not both days.

**3.3b — Pause/resume precision:** reconfirming §3.7's pause/resume applies at whatever point in the sequence the "No Power" event happens — including mid-dosing. If RL2 was on when IN1 goes LOW, dosing pauses with its remaining duration held, and resumes counting down (not restarting) once IN1 goes HIGH, same as an interrupted valve run.

**3.3c — Interval-based repeat, not weekday-based:** confirmed that calendar days (Mon/Tue/...) aren't how a farmer thinks about this — what matters is how often a zone gets watered. Repeat model changes to:
- **Interval in days:** run every N days (N=1 is daily, N=2 is alternate-day, etc.) — this replaces the weekday-checkbox picker as the primary repeat control.
- **Rotation mode (optional):** a Program can hold several Sequences that run one-per-day in rotation (Sequence 1 today, Sequence 2 tomorrow, Sequence 3 the day after, back to Sequence 1) rather than all running on the same interval — useful when a farmer wants to cycle through different zone groupings on successive days without hand-scheduling each one.
Both models persist through RTC polling (§3.3a) the same way a fixed schedule would, and both still support overnight/cross-midnight runs.

### 3.7 Safety & Fail-Safes — IN1 ("No Power") — polarity corrected
- IN1 is pulled up; **HIGH = power OK**, **LOW = No Power condition** (corrected from earlier draft — polarity was backwards).
- Behavior: while IN1 is LOW, the irrigation cycle **pauses** and **all relays (pump, dosing, all 4 valves) switch off** — any zone currently running holds its progress (elapsed time / delivered volume already counted is preserved, timers frozen), and the scheduler does not start any new zone.
- When IN1 goes HIGH, the paused cycle **resumes** from where it left off (remaining time/volume), and the scheduler resumes normal operation.
- **How the delay carries forward (worked example):** Sequence 1 starts 10:00 AM, planned to run 2 hours (due to finish 12:00 PM). A 1-hour power outage occurs partway through. Because the elapsed *run* time (not clock time) is what's preserved, Sequence 1 now finishes at 1:00 PM instead of 12:00 PM — the outage simply adds its own duration to how long the sequence takes in real time. If Sequence 2 was queued to start right after Sequence 1 (originally 12:00 PM), it does **not** start at its original clock time if Sequence 1 is still running — it waits and starts once Sequence 1 actually finishes (1:00 PM), then runs its own full 2 hours from there. In general: **only one sequence/program runs at a time** (there's one pump), so if a next schedule's start time arrives while the current one is still active — whether because of a power-outage delay or just normal overrun — it queues behind the running one rather than overlapping or being skipped.
- Manual commands issued while paused: queued, applied once resumed (consistent with the comm-loss behavior already spec'd).
- This event is logged (pause start, resume, total paused duration) and raised as an app alert, since "grid/pump power lost" is operationally important to the farmer even though the controller itself keeps running on battery.
- Everything else in §3.7 (pressure/no-flow protection) unchanged apart from the valve-combination point in §3.2 — pressure thresholds are informational alerts (farmer-configurable, enable/disable), not an automatic shutoff, so the farmer stays in control of valve combinations.

---

## 4. Mobile App — Feasibility Review of the NB Agri Automation Layout

You shared a 7-screen reference layout (NB Agri Automation — Smart Irrigation & Fertigation Controller) as the target for Kamta's v1 app. Here's what's directly buildable on Water Manager-Mini hardware today, what needs firmware work but no new hardware, and what genuinely needs hardware we don't have.

| Reference screen element | Status | Notes |
|---|---|---|
| Login page | ✅ Doable as-is | Standard pattern, reused from FG1/TH Monitor apps |
| Dashboard — Irrigation/Pump running status, running sequence/valve | ✅ Doable | Maps directly to zone/relay state |
| Dashboard — **Tank Levels** (Water/Fert A/B/C) | ❌ Not doable | No tank-level sensor input on this board. IN2/IN3 could carry a level sensor later, but only 2 spare channels total vs. 4 tanks shown |
| Dashboard — **Fertigation status** | ✅ Doable (simplified) | Single dosing channel (RL2), on/off + "dosing now" indicator — not the multi-tank display shown |
| Dashboard — **Parameters** (EC, pH, Soil Moisture, Temp, Humidity) | ❌ Not doable as shown | No such sensors wired. We do have IN2/IN3 (raw voltage, undefined) and the separate TH Monitor product already covers temp/humidity — could federate that data in later, not v1 |
| Dashboard — Quick Actions (Manual/Stop/Program/History/Alarms) | ✅ Doable | Maps to zone/pump/dosing manual control, schedule editor, history, alerts |
| Program List (name, start time, cycle, enable/disable) | ✅ Doable | Maps to per-zone or per-program schedule list |
| Sequence Selection (choose which sequences run in a program) | ⚠️ Doable, but bigger firmware scope | See §5 — needs a grouping/sequencing layer above simple per-zone schedules. No new hardware required |
| Sequence Configuration — valve selection, pump, run mode (time/quantity), water quantity | ⚠️ Doable, bigger firmware scope | Pump (RL1) is automatic (on whenever a zone in the sequence runs) — no separate "pump selection" needed, since there's only one pump relay |
| Sequence Configuration — **Fertigation & Dosing** | ✅ Doable (simplified) | One toggle (dosing on/off for this sequence) + one timing choice (start/mid/end of sequence) + one duration field. No A/B/C fertilizer quantities, no pre/post/mixed dosing modes, no quantity-based dosing — time-based only, single channel |
| Program Schedule (start time, repeat interval, auto-start) | ✅ Doable | Interval-based (daily/alternate/every-N-days) + optional rotation, not weekday picker — see §3.3c |
| Program Overview (sequence summary before submit) | ✅ Doable | Pure UI/summary screen |

**Bottom line:** with fertigation clarified as a plain timed relay switch, essentially the whole reference layout is buildable on current hardware except tank levels and the multi-sensor parameter strip (EC/pH/soil/temp/humidity) — those genuinely need sensors that aren't on this board. Note the zone count: **4 irrigation valves (RL3–RL6)**, not 6, since RL1 and RL2 are reserved for pump and dosing.

We'll use the NB Agri Automation branding/logo and visual style as directed, adapted to only the screens/fields the hardware actually supports.

### Extra things worth considering (not in the reference layout)
- **Low-pressure alert as a valve-combination guide** — the farmer sets a per-channel threshold and toggles the alert on/off; it's informational (see §3.2), helping them find their own safe max combination rather than the system enforcing one.
- **"No Power" pause indicator** — worth a dedicated dashboard chip ("Paused — grid power lost") given §3.7.
- **Multilingual UI** — the reference login screen already shows a language selector; worth confirming which languages Kamta needs (Hindi likely, given the farm context).
- **Valve/relay duty-cycle counter** — track total activations per relay for preventive maintenance flagging (relays wear out).
- **Tamper/theft detection** — flag if flow is detected while no zone is commanded on.
- **SMS fallback alerts** — you already have GSM expertise in-house (GSM Pump Controller product); for farmers without reliable data, a low-priority SMS alert channel (low battery, no-power, critical fault) could be a nice differentiator later.

---

## 5. Scheduling Engine — decision needed

The reference app's model is **Program → one or more Sequences → each Sequence runs one or more Valves together, with a pump selection and run-mode (time or volume)**. This is meaningfully richer than the "each zone has its own independent schedule" design in v1 of this spec.

Building it is fully possible on the existing relay hardware (the 74HC595/ULN2003 chain can energize any combination of RL3–RL6 together, so "Sequence 1 = Valve 1 + Valve 2" is just two relays commanded on at once, with RL1 auto-following as the pump). It's a firmware scope increase, not a hardware one. **Confirmed:** sequencing is unrestricted — pump (auto) + optional dosing + any combination of the 4 valves, with no firmware-enforced limit on how many run together.

Pump selection is resolved (§2.3) — RL1 is always the pump, auto-driven by whether any zone in the running sequence is open, so there's no per-sequence "pick a pump" field needed in the app.

---

## 6. Mobile App — Pages for Kamta v1

Adapting the reference layout to what's buildable now:

1. **Login** — User ID/mobile number + password, remember me, forgot password, language selector, NB Agri Automation branding.
2. **Dashboard** — Device selector (for future multi-device support), online/offline status, irrigation running/idle, pump status, currently running sequence/zone, battery %, flow rate + today's total, both pressure readings, IN2/IN3 raw voltage, "No Power" paused indicator when active, active alerts banner, quick actions (Manual, Stop, Program, History, Alarms), and **per-zone summary: water delivered (or duration run) + fertigation applied today**, so the farmer sees at a glance what each zone actually got, not just system-wide totals.
3. **Program List** — Named programs with start time, repeat interval (daily/alternate-day/every-N-days) or rotation, enabled/disabled toggle, add new.
4. **Sequence Selection** — Choose which sequences belong to a program, reorder, add sequence.
5. **Sequence Configuration** — Sequence name, valve(s) selection (from the 4 available zones), run mode (time-based or volume-based), run time or water quantity target, dosing toggle + timing (start/mid/end of sequence) + duration.
6. **Program Schedule** — Program name, start date, start time, repeat interval in days (or rotation across sequences), auto-start toggle, remarks.
7. **Program Overview** — Full summary of the program's sequences (valves, run time, fertigation field simply omitted) before submit.
8. **Manual Control** — Direct on/off for pump, dosing, and any/all of the 4 valves, independent of any program (reference layout's "Manual" quick action opens into this). The farmer can override any of these at any time; an override interrupts the current automated cycle and, once released/finished, the schedule resumes normally (consistent with §3.2/§3.3b's pause/resume behavior).
9. **History** — Per-zone flow, pressure, and battery trend charts + full cycle log, exportable; see §8 for retention.
10. **Alarms** — Active + historical alerts (low battery, no-power pause, pressure out-of-range, no-flow-during-run, device offline).
11. **Settings** — Device info/rename, WiFi re-provisioning, threshold configuration, firmware version/OTA, RTC status.

---

## 7. Extension Relay Board
Confirmed: **deferred, later add-on.** J7 daisy-chain connector stays on the hardware for future use; not designed into v1 firmware or app (app assumes a fixed 6, or 5 if a relay is reserved for pump — see §5).

---

## 8. History Retention & Demo Data
- **Retention target:** 3–6 months of history visible in the app.
- The ESP32's onboard flash is not the right place to hold 3–6 months of fine-grained logs — v1's local buffer (for offline resilience, per §3.5) should stay short (days, not months), with the **cloud-side database being the system of record** for the long history the app displays. The app's History screen queries the cloud, not the device, for anything beyond "right now."
- **Dummy/demo data:** for previewing the History screens during development (before real field data accumulates), we'll generate a seeded dataset — a few months of plausible daily flow totals, pressure trends, and cycle logs for a demo device — so the UI can be reviewed and iterated on before Kamta's unit has real history. This is a development/demo aid, not a firmware feature.

---

## 9. Multi-Customer Admin / Diagnostic View — role model defined, single app
Three tiers, but **one app** — not a separate web console:

- **Farmer/User:** own device(s) only.
- **Dealer** (Kamta, and future dealers): **full control** (not just view) on only the devices assigned to them — he needs to actually operate/troubleshoot a customer's system to support them, not just look at it.
- **Admin** (Santosh, Avinash): full access, every account and device, any customer.
- Same mobile app serves all three — role is tied to login, and the UI (device list, dashboard, controls) scopes itself to whatever that account can see: one device for a Farmer, assigned devices for a Dealer, everything for Admin. This replaces the earlier idea of a separate web-based admin console — not needed for v1 since the mobile app already covers every role.
- **Device registry:** every unit is tracked by its **Master ESP32 ID** (unique per board) in a central registry. Admin assigns a given device ID to a dealer's (or farmer's) account; assignment is an explicit admin action at sale/install time. Whether that assignment happens from within the app itself (an admin-only screen) or a lightweight internal tool for now is still open — see §11.

---

## 10. Connectivity / Broker
Confirmed: use the existing hosted MQTT broker for now. Since you plan to move to a broker on your own GigaNodes server once set up, firmware and app should treat the **broker address as a configurable setting** (stored, not hard-coded), so the switch later is a config/OTA push rather than a firmware rewrite.

---

## 11. What actually needs to get built
Yes — three components, all interdependent:

1. **Firmware (ESP32)** — everything in §§2–3: sensing, relay/pump/dosing control, RTC scheduling with interval/rotation logic and outage-aware queuing, IN1 pause/resume, MQTT + local fallback connectivity, OTA.
2. **Backend + database (cloud)** — the piece that didn't have its own section yet but is implied throughout: user accounts and roles (Farmer/Dealer/Admin), the device registry (Master ESP32 ID → owner/dealer), long-term history storage (§8, 3–6 months), MQTT broker (or a path to your own), alert/notification dispatch. This is the system of record everything else talks to.
3. **Mobile app (single app, role-based)** — §6's pages, rendered differently depending on whether the logged-in account is a Farmer, Dealer, or Admin, per §9.

**No separate web app is needed for v1** given the single-app-all-roles decision in §9 — that simplifies the earlier plan. A desktop/web view could still be worth adding later purely as a *convenience* for Admin/Dealer work at scale (bulk device search across hundreds of customers is easier on a bigger screen), but it's not a separate product to build now — it would just be another client against the same backend from item 2, whenever/if it's worth the effort.

## 12. Open Questions (remaining)
- **Device registry / assignment workflow** — should admin assign a Master ESP32 ID to a dealer/farmer from within the app itself, or is a lightweight internal tool (spreadsheet, simple script) good enough for now?

## 13. Resolved in this revision
- Fertigation & dosing: single channel, time-based, via RL2 — no separate hardware needed.
- Relay allocation: RL1 pump, RL2 dosing, RL3–RL6 = 4 irrigation zones.
- **IN1 polarity corrected: HIGH = power OK, LOW = No Power** (pauses cycle, all relays off).
- Outage-delay behavior: elapsed run time (not clock time) is preserved, so a delayed sequence pushes back whatever's queued after it — only one sequence runs at a time.
- Pump dependency: pump can never run without at least one valve open; last valve closing (schedule end or manual) auto-closes the pump too.
- Access model: Admin = all accounts; Dealer (Kamta) = full control on assigned devices; Farmer = own device only — all through **one app**, no separate web console for v1.
- Languages: English + Hindi.
- Sequencing: pump (auto) + optional dosing + any combination of the 4 valves, no firmware-enforced simultaneity limit — pressure-threshold alerts (farmer-configurable) inform the choice instead.
- Dosing duration: short relative to the irrigation cycle (e.g., ~10 min within a 3-hour run) — no hard min/max enforced.
- Scheduling: interval-based (daily/alternate/every-N-days) or rotation, not weekday-based; overnight/cross-midnight runs supported.
- Dashboard shows per-zone water/duration + fertigation applied.
- Manual override covers pump, dosing, and valves, at any time.
