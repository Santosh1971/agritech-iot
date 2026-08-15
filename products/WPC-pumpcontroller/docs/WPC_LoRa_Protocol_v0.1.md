# WPC — LoRa Protocol Design (v0.1)

**Scope:** Master ↔ Pump Node communication over LoRa (SX1262). Provisioning (Master ID + slot) happens out-of-band via the Pump Node's SoftAP over WiFi — not covered here.

---

## 1. Packet Structure

| Field | Size | Notes |
|---|---|---|
| Version | 1 byte | protocol version, for future compatibility |
| Msg Type | 1 byte | see §2 |
| Master ID | 4 bytes | lower 4 bytes of the Master's MAC address |
| Pump Slot | 1 byte | 0–19 (0xFF = unassigned/broadcast) |
| Sequence # | 1 byte | rolling counter, detects duplicate/stale packets |
| Payload | 0–N bytes | message-specific |
| CRC16 | 2 bytes | corruption check over the long-range link |

10 bytes fixed overhead.

## 2. Message Types

| Type | Direction | Payload | Purpose |
|---|---|---|---|
| `LEVEL_CMD` | Master → Pump | 1 byte: ON/OFF | Commands the pump; doubles as the heartbeat the pump watches for |
| `CMD_ACK` | Pump → Master | 1 byte relay state, 1 byte analog status, 1 byte no-power flag | Confirms command applied + reports status |
| `JOIN_REQUEST` | Pump → Master | 4 bytes: pump's own MAC-derived ID | Sent on boot so Master's node table (and app) picks up the pump |
| `JOIN_ACCEPT` | Master → Pump | — | Confirms the pump is recognized |

## 3. Operational Model — Master Polls, Pumps Don't Speak Unsolicited

With up to 20 nodes on one channel and a single Master, letting Pump Nodes transmit freely invites collisions as node count grows. Instead:

- **Round-robin poll cycle**: each cycle, the Master sends a `LEVEL_CMD` (real command or "no change") to each pump in turn and waits briefly for `CMD_ACK` before moving on.
- This single mechanism is both **command delivery** and **heartbeat** — no separate heartbeat packet.
- **Staggered start**: when the level logic decides several pumps should go OFF→ON in the same cycle, the Master spaces those specific `LEVEL_CMD` sends 5 seconds apart within its poll pass.
- **Fail-safe lives on the Pump side**: each Pump Node tracks "last command received." If no `LEVEL_CMD` addressed to its slot arrives within 3x the expected poll interval, it force-cuts the relay to OFF locally — independent of the last command, so it fails safe even if the Master itself has gone down, not just on a single dropped packet.

See the attached diagram for the provision → join → poll cycle → fail-safe flow.

## 4. Open Items (carried from the spec)

- Exact poll interval / cycle time (depends on the LoRa link-budget test at 1-2 km).
- Session-key/authenticated pairing — deferred per project decision; plaintext Master-ID matching for now.
- LED blink pattern definitions for normal/fault/no-power states.
