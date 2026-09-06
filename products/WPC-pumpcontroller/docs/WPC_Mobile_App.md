# WPC — Mobile App Reference (v1, 6 Sep 2026)

**Scope:** the Flutter app in `mobile-app/wpc_app/`. Covers what each screen does, why, and which HTTP calls (see `WPC_LoRa_Protocol_v0.3.md` §6–7) it makes. This is the single common app used by both the operator (day-to-day status/config) and the field installer (pump provisioning) — there is no separate installer app, and no password gate on the installer features yet (open item, see Specification §9).

## Connectivity model

The app talks to whichever WPC device the phone's WiFi is currently connected to, always at the fixed gateway address `http://192.168.4.1` (`api.dart`) — this works unmodified whether the phone is on a Master's SoftAP (`WPC-Master-XXXXXXXX`) or a Pump's own SoftAP (`WPC-Pump-XXXX`), since only one is ever connected at a time and both act as the gateway on their own AP. **The app does not manage WiFi switching itself** — the user manually joins the right network from their phone's WiFi settings before using a given screen, and each screen that needs a specific network (Provision) says so explicitly in-app.

`api.dart` is a thin static HTTP client with one method per endpoint; it has no business logic beyond building the request and throwing on a non-200 response. See the protocol doc for exact request/response shapes.

## Screens (`main.dart`'s three-tab shell)

### 1. Status (`status_screen.dart`) — connect to the **Master's** SoftAP

The day-to-day operator dashboard. Polls `GET /status` every 3 seconds.

- Shows the Master ID (tap-to-copy), a "No Power" alert banner if that input is active, and one box per configured level (highest level at top, matching the physical board layout), each listing the pumps assigned to it.
- Each pump row shows: online/offline, running/idle (with distinct colors/icons for each combination), its raw IN1/IN4 ADC readings (labeled "raw" — no calibrated units yet, see Specification §3.3), and a **Manual** switch. Toggling Manual on reveals ON/OFF choice chips that call `POST /override` directly — this is the operator-facing manual control described in Specification §5.
- Pumps with no level assignment yet appear in a separate "Unassigned" section rather than under any level box.
- A small hand icon appears next to any pump currently in manual override, so it's visually obvious at a glance which pumps aren't under automatic level control.

### 2. Assign (`assign_screen.dart`) — connect to the **Master's** SoftAP

Operator-level configuration, not day-to-day monitoring. Fetches `GET /status` once on load (not polled continuously — this screen is for occasional config changes, not live monitoring).

- **Number of Levels** (1–3): a segmented button, calls `POST /config {numLevels}`.
- **Level Debounce Time** (10s–300s slider): the minimum-dwell time before a level-switch reading is accepted, calls `POST /config {debounceMs}`.
- **Master Radio TX Power** (-9 to +22 dBm slider): calls `POST /config {txPower}`. Explicitly labeled as only affecting the Master's own transmissions — the in-app copy reminds the operator that each Pump's TX power (Provision screen) needs setting separately for range to change in both directions.
- **Available Pumps** list: rename (`POST /name`) or forget (`POST /forget`, with a confirmation dialog explaining it doesn't affect the physical Pump Node — it can rejoin later) any known pump.
- **Per-level assignment chips**: one `FilterChip` per pump per level, toggling that pump's membership in that level via `POST /assign`.

All slider/toggle handlers here follow the same pattern: set a local "busy" flag, call the API, re-fetch status, clear busy — disabling the relevant controls only for the duration of that specific action (see the flicker note below for why this matters).

### 3. Provision (`pump_screen.dart`) — connect to a **Pump Node's own** SoftAP (`WPC-Pump-XXXX`)

The installer-facing screen for pointing one physical Pump Node at a Master, viewing its live identity, and tuning its own radio. An in-app banner reminds the installer to switch WiFi networks before using it, since it's easy to have the Master's AP still connected from the previous screen.

Polls `GET /info` every **1 second** (not 3s, unlike the other screens) — this is a direct WiFi connection to the Pump's own SoftAP with no LoRa airtime cost, and fast feedback matters here since this is the screen used for live field calibration against IN1/IN4 while physically adjusting a sensor or wiring.

- **Current Identity** card: pump ID, target Master ID, joined/not-joined (with a note that joining can take up to ~15s), current relay state, and live raw IN1/IN4 readings.
- **Link to Master**: an 8-character hex text field + Save button, validated client-side against `^[0-9A-F]{8}$` before calling `POST /config {targetMasterId}`. Saving forces the Pump to rejoin under the new Master (the field firmware resets `joined=false` whenever this changes).
- **Pump Radio TX Power** (-9 to +22 dBm slider): calls `POST /config {txPower}` on *this* Pump Node specifically — same reminder as the Assign screen's TX power control, in reverse (set the Master separately).

**Implementation note for future readers — a flicker bug and its fix:** the periodic 1-second auto-refresh (`_fetch()`) used to also toggle the screen's `_busy` flag, which is the same flag that disables the Save button and the TX power slider. At a 1-second refresh rate this caused those controls to visibly flicker enabled/disabled continuously. The fix: `_fetch()` no longer touches `_busy` at all — only explicit user-initiated actions (`_save()`, the TX slider's `onChangeEnd`) set `_busy` themselves around their own `await` calls. If you add a new auto-polled field to this screen, don't gate any interactive control on a busy flag that a background timer also flips.

## Things intentionally NOT in the app (yet)

- No installer password/PIN gate (Specification §9, open item).
- No calibrated display of IN1/IN4 (raw ADC counts only — calibration is deferred, see Specification §3.3).
- No display of the Pump's own digital IN1/IN4 boolean flags or LED states (only the analog raw values) — these exist in the wire protocol but aren't currently surfaced.
- No data logging or history — every screen shows only the current live snapshot.
