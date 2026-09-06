# WPC — LoRa Protocol & HTTP API Reference (v0.3, updated 6 Sep 2026)

**Scope:** Master ↔ Pump Node LoRa wire protocol, both nodes' HTTP APIs, and the Master's poll/pacing algorithm. Supersedes v0.2 — the CMD_ACK payload grew, a new HTTP endpoint was added, and the poll scheduling was redesigned. Sections carried over unchanged from v0.2 are marked as such.

---

## 1. Packet Structure (unchanged since v0.1)

| Field | Size | Notes |
|---|---|---|
| Version | 1 byte | protocol version (currently 1) |
| Msg Type | 1 byte | see §2 |
| Master ID | 4 bytes | lower 4 bytes of the Master's MAC address |
| Pump Slot | 1 byte | 0–19 (0xFF = unassigned/broadcast) |
| Sequence # | 1 byte | rolling counter |
| Payload | 0–N bytes | message-specific, see §2 |
| CRC16 | 2 bytes | CRC16-CCITT, poly 0x1021, init 0xFFFF |

## 2. Message Types & Payloads

| Type | Direction | Payload | Notes |
|---|---|---|---|
| `JOIN_REQUEST` (0x01) | Pump → Master | 2 bytes: pumpId (uint16, big-endian) | Sent every 1.2s while unjoined. Pump Slot field = 0xFF. |
| `JOIN_ACCEPT` (0x02) | Master → Pump | 3 bytes: pumpId (2 bytes, echoed) + assigned slot (1 byte) | Echoes the accepted pumpId so a pump can verify the accept was meant for it (LoRa is broadcast). |
| `LEVEL_CMD` (0x10) | Master → Pump | 1 byte: ON/OFF | Also serves as the liveness signal the Pump's fail-safe timer watches. |
| `CMD_ACK` (0x11) | Pump → Master | **7 bytes (grew from 3 in this rev)** — see §2.1 | |

### 2.1 CMD_ACK payload (v0.3 — changed)

| Byte offset (from payload start) | Field | Notes |
|---|---|---|
| 0 | relay state | 0/1, digital, unchanged from v0.2 |
| 1 | IN1 digital | 0/1, active-low boolean, unchanged from v0.2 |
| 2 | IN4 digital | 0/1, active-low boolean, unchanged from v0.2 |
| 3–4 | IN1 raw ADC | uint16, big-endian, 0–4095 — **new in v0.3** |
| 5–6 | IN4 raw ADC | uint16, big-endian, 0–4095 — **new in v0.3** |

The Master's `pollPump()` requires the received packet to be at least 17 bytes total (8-byte header + 7-byte payload + 2-byte CRC) before trusting bytes 3–4/5–6 as ADC data, so a shorter/legacy 3-byte-payload ACK (13 bytes total) is still accepted for relay/digital status but simply leaves the cached `in1Adc`/`in4Adc` at their previous value rather than misreading garbage.

There is no separate "telemetry" message type — IN1/IN4 readings are only ever piggybacked on whatever CMD_ACK the normal poll cycle already produces (see §5). A fresh reading only reaches the Master when that pump is actually polled.

## 3. Identity & Defaults — unchanged from v0.2

- **Master ID** = lower 4 bytes of the Master's own MAC (`ESP.getEfuseMac()`), computed at boot.
- **Pump ID** (4-digit, 0–9999) = `MAC % 10000` by default, overridable via NVS / the app's Provision screen.
- **Target Master ID** on the Pump defaults to a compile-time `DEFAULT_MASTER_ID`, overridable the same way. Two independent WPC installations near each other will collide on this default until explicitly provisioned — still an open item (see Specification §9).
- **Syncword**: derived per-Master from its own ID (XOR-fold of all 4 bytes), giving radio-level isolation between independent WPC systems before a packet is even decoded.

## 4. Join / Registration Flow — unchanged from v0.2

1. Pump boots unjoined, sends `JOIN_REQUEST` every 1.2s.
2. Master listens for `JOIN_REQUEST` during dedicated listening windows (see §5 — these now happen far more often than v0.2's single fixed window per loop pass).
3. On a valid request addressed to this Master: reuses the pump's existing slot if known, otherwise assigns the next free slot; persists the mapping to NVS immediately; replies with `JOIN_ACCEPT`.
4. A fresh join resets that slot's cached ADC values and manual override to defaults (off/auto) — a slot being reused may now belong to a different physical unit than before.
5. Pump verifies the echoed pumpId matches its own before accepting.

## 5. Poll Cycle & Pacing (rewritten in v0.3)

**v0.2 model (retired):** a per-pump 30-second "heartbeat" timer, re-sending the last command on a fixed schedule to keep state/telemetry fresh, layered on top of immediate sends on real state changes, plus a separate 5-second "stagger" delay specifically when multiple pumps transitioned OFF→ON in the same pass.

**v0.3 model:** `pollCycle()` services **exactly one pump per call**, then blocks for a fixed `INTER_POLL_GAP_MS` (5000ms) before returning. There is no per-pump timer and no separate stagger logic — one mechanism does both jobs:

1. **Priority 1 — state change:** if any known, previously-contacted pump's desired state (from level logic or manual override) no longer matches its last confirmed relay state, that pump is serviced immediately, ahead of the round-robin.
2. **Priority 2 — round-robin refresh:** otherwise, the next known pump in a persistent round-robin cursor is serviced. This is what refreshes `online`/`offline` status and IN1/IN4 telemetry for pumps with nothing new to command.
3. After the exchange (whether it ACKed or not), the Master calls `listenForJoin(INTER_POLL_GAP_MS)` — i.e. the mandatory pacing gap **is** a join-listening window, not idle time. This means new/rejoining pumps get a listening opportunity roughly every 5 seconds regardless of how many pumps are already known, rather than v0.2's single ~2-second window per full loop pass.

**Consequences of this design:**
- A full refresh of **N** known pumps takes **~N × 5 seconds** — automatic, not configured. (There used to be an app-facing "Pump Refresh Interval" slider for this in an earlier iteration of this rev; it was removed once the design changed to make the cadence a physics/topology-derived constant rather than a user preference.)
- No two Master transmissions, to any pump, for any reason, are ever less than 5 seconds apart — this is what makes the original "5s between simultaneous pump starts" inrush-avoidance requirement automatic, without needing to specifically detect simultaneous ON transitions the way v0.2 did.
- `loraLinkError` (drives the Master's LoRa-LED fast-blink fault pattern) is now judged over a **rolling count of consecutive failures across calls**, rather than "did every known pump fail in one full sweep" — it only trips once consecutive misses reach the current known-pump count, so one dead/out-of-range pump doesn't flag the whole link down while others are acking fine.
- **`INTER_POLL_GAP_MS` = 5000 is an unvalidated placeholder**, chosen because a round trip at the 1–2km target range could plausibly take several seconds including retries — propagation delay itself is negligible at that distance; the real driver is LoRa airtime at the configured SF9/125kHz plus retry overhead. It has only been bench-tested at <1m. Revisit this constant (and the `POLL_TIMEOUT_MS`/TX timeout values inside `pollPump()`, currently sized for a fast bench link) together once real range data exists.

The retry/timeout budget *within* a single pump's exchange is unchanged from v0.2: a pump that's `online` (or never yet attempted) gets the full budget (up to 3 attempts, 2s TX timeout, 500ms RX window); a pump already confirmed offline gets a single short attempt (500ms TX timeout, 200ms RX window) so a known-dead slot doesn't eat into other pumps' turn.

## 6. HTTP API — Master Node

Master runs an open WiFi SoftAP (`WPC-Master-XXXXXXXX`, no password) with a synchronous web server on port 80. All endpoints send `Access-Control-Allow-Origin: *`.

### `GET /status`
```json
{
  "masterId": "0x68A99B20",
  "numLevels": 3,
  "debounceMs": 10000,
  "txPower": 14,
  "levels": [false, true, false],
  "noPower": false,
  "pumps": [
    {
      "slot": 1,
      "pumpId": 2368,
      "online": true,
      "relay": true,
      "desired": true,
      "assignedLevels": [2],
      "name": "",
      "in1Adc": 2229,
      "in4Adc": 331,
      "override": { "enabled": false, "state": false }
    }
  ]
}
```
`in1Adc`/`in4Adc` and `override` are new in v0.3. `desired` reflects the *computed* target state (from level logic or override); `relay` reflects the last *confirmed* (acked) state — they can briefly disagree while a command is in flight.

### `POST /config`
Body may include any subset of:
```json
{ "numLevels": 1-3, "debounceMs": 10000-300000, "txPower": -9 to 22 }
```
`txPower` is new in v0.3 — applied live via `radio.setOutputPower()`, persisted to NVS. Only affects the Master's own transmissions; the matching Pump's TX power must be set separately (see §7).

### `POST /assign`
```json
{ "slot": 1, "level": 2, "assigned": true }
```
Toggles one level's membership for a pump — unchanged from v0.2.

### `POST /override` — new in v0.3
```json
{ "slot": 1, "enabled": true, "state": true }
```
`enabled: false` returns the pump to automatic (level-logic) control. `enabled: true` with `state` forces the relay to that value until changed again. Applied immediately (`applyLevelLogic()` is re-run synchronously in the handler, not left for the next loop pass). **Not persisted** — a Master reboot always resets every pump to automatic.

### `POST /name`
```json
{ "slot": 1, "name": "Field Pump A" }
```
Unchanged from v0.2.

### `POST /forget`
```json
{ "slot": 1 }
```
Clears a slot entirely, including its ADC cache and override state (new in v0.3) — unchanged behavior otherwise. Does not affect the physical Pump Node; it can rejoin later.

## 7. HTTP API — Pump Node

Pump Node runs its own open SoftAP (`WPC-Pump-XXXX`, XXXX = its 4-digit pump ID), separate from the Master's — connecting to it is how the app's Provision screen reaches this endpoint set.

### `GET /info`
```json
{
  "pumpId": 2368,
  "targetMasterId": "0x68A99B20",
  "joined": true,
  "assignedSlot": 1,
  "relay": true,
  "in1Adc": 2229,
  "in4Adc": 331,
  "txPower": 14
}
```
`relay`, `in1Adc`, `in4Adc`, and `txPower` are all new in v0.3 — `relay` and the ADC fields are sampled fresh on every request (no caching), so this reflects the pump's live state, not what it last reported to the Master.

### `POST /config`
```json
{ "pumpId": 1234, "targetMasterId": "68A99B20", "txPower": 14 }
```
`pumpId`/`targetMasterId` unchanged from v0.2 (changing either forces a rejoin, since the identity or target changed). `txPower` is new in v0.3, applied live, no rejoin needed — it doesn't affect identity, only what this radio transmits at.

## 8. LED Reference

See `WPC_Specification_v0.3.md` §4 for the full table (Pump Node LEDs, corrected pin assignments and the new LoRa-activity blink). Master Node LED behavior is unchanged from v0.2 (WiFi status pattern, LoRa TX/RX blink + link-error fast-blink, per-level status LEDs).

## 9. Open Items (carried over from v0.2 unless noted)

- Pairing/join security — deliberately deferred (plaintext IDs, open APs).
- Installer password scheme for the app's node-assignment feature — still not implemented.
- Multi-Master coexistence — unchanged limitation, see Specification §9.
- LoRa link-budget validation at real 1–2km range — **now directly affects `INTER_POLL_GAP_MS` and the poll timeouts in §5**, not just theoretical range; only bench-tested at <1m so far.
- IN1/IN4 calibration (raw → real units) — deferred, see Specification §3.3.
