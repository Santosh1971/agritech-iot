# WPC — Backlog / Next Steps (as of 15 Aug end of session)

Rough priority order, not a rigid sequence — pick based on what you want to see working next.

## Firmware

1. **Real per-pump level assignment** — replace the IN1→slot0/IN2→slot1 test mapping with an actual configurable trigger-level (and possibly stop-level, for hysteresis) per pump, stored in Master's NVS. This is the biggest remaining gap between "bench test" and "real product logic."
2. **Master write endpoints** — `/status` is read-only. Needs at minimum a way to set each pump's level assignment, and ideally rename/forget a pump.
3. **Multi-Master coexistence** — every Pump currently defaults to the same hardcoded Master ID. Needs a real solution before two systems could ever sit near each other in the field (per the original spec's core requirement).
4. **LED blink patterns** — normal/fault/no-power states still undefined beyond "some LED lights up."

## App

1. **Level-assignment screen** — depends on firmware item #1/#2 above.
2. **Pump management** — rename/forget/view-detail for a joined pump.
3. **Provisioning screen** — write pumpId/masterId overrides into a Pump's NVS (needs the Pump to expose *its own* SoftAP + endpoint too — doesn't exist yet, only Master has one).
4. **Password-gated installer feature**, per the earlier decision (single app, not two).

## Hardware / Field-Readiness

1. Real range test (1-2km target, only tested at <1m so far).
2. Float switches instead of DIP switch test proxies.
3. Final PCB — pin numbers need re-verification once it arrives (bench setup used breadboard/dev-kit wiring cross-checked against the PCB schematic, but not the literal manufactured board).

## Deferred by design (not forgotten, just intentionally later)

- Pairing/join security (plaintext for now)
- Remote monitoring / internet connectivity (Kamta may request later)
- Data logging
