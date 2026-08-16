# WPC — Test Plan (for 16 Aug session)

Everything below is either unverified, only lightly tested, or a known gap surfaced during today's (15 Aug) debugging. Rough priority order.

## A. Confirm today's fixes hold up

- [ ] Reset Master with both Pumps running → confirm NVS restore (`[NVS] restored slot ...`) and **no** rejoin needed, polling resumes within a few seconds.
- [ ] Reset **one** Pump only → confirm it re-joins automatically (`[FAILSAFE] lost contact ... rejoining` → `[JOIN] accepted`) and lands back on the **same** slot (tests the `findSlotByPumpId` stable-reassignment path, not just `findFreeSlot`).
- [ ] App: confirm `/status` stays reliably responsive now (not intermittent) over a longer session — a few minutes of continuous polling, not just one load.

## B. Untested edge cases in current logic

- [ ] **Simultaneous fail-safe on both Pumps at once** (e.g. power off Master with both pumps ON) — confirm both independently fail-safe to OFF and both later rejoin without colliding on slots again.
- [ ] **A pump joining while another is mid-poll** — start a fresh (never-joined) 3rd board if available, confirm it doesn't disrupt the two already-running pumps' poll cycle.
- [ ] **Hysteresis** — not actually implemented yet; current IN1/IN2 logic is a direct level-follow, not a trigger/stop band. Worth deciding whether to build this before or after the app's level-assignment screen, since the UI shape depends on it (single threshold vs. two).
- [ ] **OFF-side stagger** — confirmed intentional (no stagger needed on shutdown), but worth a sanity check that near-simultaneous OFF doesn't cause any radio collision issues at the poll-cycle level.

## C. Real-world conditions not yet tested

- [ ] **Actual range** — everything so far has been bench-tested at <1m. The 1-2km target (spec §9) has never been validated. Needs an outdoor test with the two boards genuinely separated.
- [ ] **LoRa duty cycle / airtime at scale** — only 2 pumps tested; the join-window (2s) + per-pump poll overhead needs checking against the 20-pump target to see if a full poll cycle stays reasonable.
- [ ] **Float switch behavior with real water**, not a DIP switch proxy — polarity was confirmed correct in principle, but the DIP switches are still standing in for the actual float switches.

## D. App-specific

- [ ] Test app behavior when Master is *unreachable* for an extended period (not just a few seconds) — does the "Not reachable" state recover cleanly once the phone reconnects to the AP?
- [ ] Test switching WiFi networks on the phone (e.g. accidentally reconnecting to home WiFi) — confirm the error state is clear enough for a non-technical user (this matters for the eventual real end user, not just bench testing).
- [ ] Android cleartext-HTTP fix (`network_security_config.xml`) — confirm it survives a `flutter clean` / full rebuild, since manifest/resource changes can sometimes get silently dropped by build caching issues.

## E. Not yet started — flag only, no action needed tonight

- Real per-pump level assignment (config, not just IN1→slot0/IN2→slot1 test wiring)
- App write endpoint on Master (currently only GET /status exists)
- Installer provisioning screen (pumpId/masterId override via app)
- Multi-Master coexistence handling
