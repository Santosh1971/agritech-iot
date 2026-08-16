# WPC — LoRa Protocol Reference (v0.2, updated 15 Aug)

**Scope:** Master ↔ Pump Node communication over LoRa (SX1262). This supersedes v0.1 — the payload formats below reflect what's actually implemented and working on the bench, not the original design sketch.

---

## 1. Packet Structure (unchanged from v0.1)

| Field | Size | Notes |
|---|---|---|
| Version | 1 byte | protocol version |
| Msg Type | 1 byte | see §2 |
| Master ID | 4 bytes | lower 4 bytes of the Master's MAC address |
| Pump Slot | 1 byte | 0–19 (0xFF = unassigned/broadcast) |
| Sequence # | 1 byte | rolling counter |
| Payload | 0–N bytes | message-specific, see §2 |
| CRC16 | 2 bytes | CRC16-CCITT, poly 0x1021, init 0xFFFF |

## 2. Message Types & Payloads (as actually implemented)

| Type | Direction | Payload | Notes |
|---|---|---|---|
| `JOIN_REQUEST` (0x01) | Pump → Master | 2 bytes: pumpId (uint16, big-endian) | Sent every 1.2s while unjoined. Pump Slot field = 0xFF. |
| `JOIN_ACCEPT` (0x02) | Master → Pump | 3 bytes: pumpId (2 bytes, echoed) + assigned slot (1 byte) | **Critical fix (15 Aug):** originally only carried the slot number. Since LoRa is broadcast, any other unjoined Pump hearing it would grab the same slot. Now echoes the accepted pumpId so each Pump can verify the accept was actually meant for it. |
| `LEVEL_CMD` (0x10) | Master → Pump | 1 byte: ON/OFF | Also serves as the heartbeat the Pump's fail-safe timer watches. |
| `CMD_ACK` (0x11) | Pump → Master | 3 bytes: relay state, IN1 status, IN4 (no-power) status | |

## 3. Identity & Defaults

- **Master ID** = lower 4 bytes of the Master's own MAC (`ESP.getEfuseMac()`), computed at boot — not user-configurable, no NVS needed.
- **Pump ID** (4-digit, 0–9999) = `MAC % 10000` by default, unique per physical board automatically. Overridable via NVS (`Preferences`, namespace `"wpc"`, key `"pumpId"`) — not yet writable by anything (app doesn't have a provisioning screen yet).
- **Target Master ID** on the Pump defaults to a compile-time constant `DEFAULT_MASTER_ID` (currently hardcoded to today's bench Master, `0x86470968`). Also NVS-overridable (key `"masterId"`), also not yet app-writable.
- **Known limitation:** since every Pump ships with the same `DEFAULT_MASTER_ID`, two independent WPC installations near each other would both try to join whichever Master they hear first. This needs solving via the app's provisioning screen before real multi-site deployment — not yet built.

## 4. Join / Registration Flow

1. Pump boots unjoined, sends `JOIN_REQUEST` every 1.2s.
2. Master listens for `JOIN_REQUEST` in a dedicated 2-second window each loop pass (`listenForJoin()`).
3. On a valid request: Master checks if this `pumpId` already has a slot (`findSlotByPumpId`) — reuses it if so (stable across Pump reboots), otherwise assigns the next free slot (`findFreeSlot`).
4. Master persists the pumpId↔slot mapping to NVS immediately (`savePumpTable()`), replies with `JOIN_ACCEPT`.
5. Pump verifies the echoed pumpId matches its own before accepting; sets `joined = true`.

**Master reboot recovery:** join table is restored from NVS on boot (`loadPumpTable()`) — if Pumps are still running (didn't lose their own `joined` state), polling resumes immediately, no rejoin needed.

**Pump reboot / comms-loss recovery:** Pump's fail-safe (§5) resets `joined = false` on timeout, triggering fresh `JOIN_REQUEST`s automatically — no manual intervention needed on either side.

## 5. Poll Cycle & Fail-Safe

- Master round-robins known (joined) slots each pass: `LEVEL_CMD` → wait for `CMD_ACK` (up to 3 attempts, 500ms each).
- **Staggered start:** when multiple pumps transition OFF→ON in the *same* poll pass, a 5-second gap is inserted between each — tracked via each pump's last *confirmed* relay state, not a lifetime "ever commanded" flag (an earlier bug used the latter and silently stopped staggering after each pump's first-ever activation).
- **Fail-safe (Pump side):** if no `LEVEL_CMD` arrives within 15 seconds, the Pump force-cuts its relay to OFF locally *and* resets to unjoined — this applies regardless of current relay state (an earlier version only checked when the relay was ON, so an idle pump that lost its Master would never notice).

## 6. HTTP Status API (new, 15 Aug)

Master runs an open WiFi SoftAP (`WPC-Master-XXXX`, no password — matches the deferred-security decision) with a synchronous web server.

**`GET /status`** → JSON:
```json
{
  "masterId": "0x86470968",
  "levels": {"in1": true, "in2": false, "in3": true, "in4": false},
  "pumps": [
    {"slot": 0, "pumpId": 9160, "online": true, "relay": true, "desired": true}
  ]
}
```

**Known limitation:** the web server is synchronous and the LoRa poll loop blocks for multi-second stretches. `server.handleClient()` is now called inside the LoRa wait loops (not just once per pass) to keep it responsive — this was needed to fix an early "loads once then disappears" bug in the app. Still not truly async; a proper fix would move to an async web server if response latency ever becomes a real problem at higher pump counts.

## 7. Open Items (unchanged from v0.1 unless noted)

- Pairing/join security — deliberately deferred (plaintext IDs, open AP)
- LED blink pattern definitions for fault/no-power states
- Installer password scheme for the app's node-assignment feature
- Multi-Master coexistence (§3) — new item, surfaced by today's implementation
- LoRa link-budget validation at real 1–2km range (only bench-tested at <1m so far)
