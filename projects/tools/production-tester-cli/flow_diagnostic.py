#!/usr/bin/env python3
"""
Standalone flow-sensor diagnostic -- nothing else. Pulses the jig
continuously at a fixed, slow rate and polls the DUT's liters_delivered
every 10s, printing readings 20 per line. Deliberately separate from the
guided production-test flow: no login, no backend, no report, no config
file -- just jig + DUT over USB-serial, to isolate whether the DUT's
flow-sensor interrupt counts anything at all when driven at a rate it
should trivially be able to keep up with.

Every previous flow_sensor result this whole project has been a single
burst-then-check (450 pulses at ~1kHz, check once) -- if the interrupt
or wiring has any issue at all, that gives no insight into WHAT's wrong,
just that it's wrong. Slow, steady, individually-visible pulses with
frequent readings should show the difference between "genuinely nothing
is being counted" (wiring/interrupt truly dead) vs. "something counts,
but not reliably" (debounce/ISR timing issue) vs. "counts fine at this
rate, so the earlier 1kHz burst rate was the actual problem".

Setup: the DUT needs to already be flashed and sitting on its own SoftAP
(use the main tool's "Run Tests Only" flow, or just power it on freshly
flashed) -- this script doesn't flash or provision anything, only
pulses and reads.

Run:
    python3 flow_diagnostic.py --device-id SWC_001_B468
    python3 flow_diagnostic.py --device-id SWC_001_B468 --rate-hz 2 --interval-s 10

Ctrl+C to stop -- prints a final summary either way.
"""
from __future__ import annotations

import argparse
import signal
import time

from production_tester.hardware_discovery import find_hardware


class _StopRequested(Exception):
    pass


def _handle_sigterm(signum, frame):
    raise _StopRequested()


def _free_run(jig, port: str, rate_hz: float) -> None:
    """Just pulses forever at rate_hz -- no SoftAP join, no HTTP querying
    of the DUT at all. For watching a DUT-side LED wired to the flow
    sensor pin directly, instead of reading liters_delivered back --
    simpler, and rules out any doubt about the query path itself while
    isolating purely the pulse-out -> DUT-pin connection.

    Auto-reconnects on a USB error instead of crashing -- 2026-09-18
    bench finding: this bench's USB has been flaky all session
    (esptool errors, port disconnects), and an unhandled crash here
    running in the background looks identical to "pulsing stopped for
    some other reason" from the outside (the jig's LED just stops, no
    visible error) -- worth surfacing clearly and recovering rather than
    silently dying.
    """
    from production_tester.jig_client import JigClient

    interval_s = 1.0 / rate_hz
    print(f"Free-running at {rate_hz} Hz. Watch the DUT's LED now. Ctrl+C to stop.\n")
    count = 0
    reconnects = 0
    start = time.monotonic()
    last_report = start
    try:
        next_pulse_at = time.monotonic()
        while True:
            try:
                ok = jig.pulse(1)
                if not ok:
                    print("  [no OK reply from jig on that pulse]")
            except Exception as e:
                reconnects += 1
                print(f"\n  [USB error: {e} -- reconnecting (attempt {reconnects})...]")
                try:
                    jig.close()
                except Exception:
                    pass
                jig = None
                while jig is None:
                    time.sleep(1.0)
                    try:
                        candidate = JigClient(port)
                        if candidate.ping():
                            jig = candidate
                        else:
                            candidate.close()
                    except Exception:
                        pass
                print("  [reconnected]")
                next_pulse_at = time.monotonic()
                continue
            count += 1
            now = time.monotonic()
            if now - last_report >= 10.0:
                print(f"{count} pulses sent ({now - start:.0f}s elapsed, {reconnects} reconnect(s))")
                last_report = now
            next_pulse_at += interval_s
            sleep_for = next_pulse_at - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)
    except (KeyboardInterrupt, _StopRequested):
        pass
    finally:
        try:
            jig.close()
        except Exception:
            pass
    elapsed = time.monotonic() - start
    print(f"\n---- summary ----\n{count} pulses sent over {elapsed:.0f}s "
          f"(~{count / max(elapsed, 0.001):.2f} Hz actual), {reconnects} reconnect(s)")
    elapsed = time.monotonic() - start
    print(f"\n---- summary ----\n{count} pulses sent over {elapsed:.0f}s (~{count / max(elapsed, 0.001):.2f} Hz actual)")


def main() -> None:
    signal.signal(signal.SIGTERM, _handle_sigterm)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device-id", help="DUT's SoftAP name, e.g. SWC_001_B468 (not needed with --free-run)")
    parser.add_argument("--rate-hz", type=float, default=2.0, help="Pulse rate (default: 2 Hz)")
    parser.add_argument("--interval-s", type=float, default=10.0, help="Seconds between readings (default: 10)")
    parser.add_argument("--per-line", type=int, default=20, help="Readings per printed line (default: 20)")
    parser.add_argument("--ppl", type=int, default=450, help="Pulses per liter, for the expected-vs-actual comparison (default: 450)")
    parser.add_argument("--free-run", action="store_true",
                         help="Just pulse forever at --rate-hz, no SoftAP join, no DUT querying -- "
                              "for watching a DUT-side LED directly instead of reading liters_delivered.")
    args = parser.parse_args()

    pulse_interval_s = 1.0 / args.rate_hz
    pulses_per_reading = max(1, round(args.interval_s * args.rate_hz))

    print(f"Finding hardware...")
    hw = find_hardware()
    if hw.jig is None:
        print("No jig found -- check the USB connection.")
        return
    jig = hw.jig
    print(f"Jig on {hw.jig_port}.")

    if args.free_run:
        _free_run(jig, hw.jig_port, args.rate_hz)
        return

    if not args.device_id:
        print("--device-id is required unless --free-run is given.")
        jig.close()
        return

    print(f"Joining DUT's SoftAP as {args.device_id}...")
    if not jig.join_dut_softap(args.device_id):
        print("Could not join the DUT's SoftAP -- check it's powered on, freshly flashed, and broadcasting.")
        jig.close()
        return
    print("Joined.")

    before = jig.device_info(timeout_s=8)
    baseline_liters = (before or {}).get("liters_delivered", 0.0)
    print(f"Baseline liters_delivered: {baseline_liters}")
    print(f"Pulsing at {args.rate_hz} Hz ({pulses_per_reading} pulses per {args.interval_s:.0f}s reading), "
          f"{args.per_line} readings per line. Ctrl+C to stop.\n")

    total_pulses_sent = 0
    readings: list[tuple[int, float, float]] = []  # (total_pulses_sent, liters_delivered, expected)
    line_count = 0

    try:
        while True:
            next_pulse_at = time.monotonic()
            failed_sends = 0
            for _ in range(pulses_per_reading):
                if not jig.pulse(1):
                    failed_sends += 1
                total_pulses_sent += 1
                next_pulse_at += pulse_interval_s
                sleep_for = next_pulse_at - time.monotonic()
                if sleep_for > 0:
                    time.sleep(sleep_for)

            info = jig.device_info(timeout_s=8)
            liters = (info or {}).get("liters_delivered")
            expected = total_pulses_sent / args.ppl
            readings.append((total_pulses_sent, liters, expected))

            shown = "?" if liters is None else f"{liters:.4f}"
            print(f"{shown}", end="  ")
            line_count += 1
            if line_count % args.per_line == 0:
                print()
            if failed_sends:
                print(f"\n  [{failed_sends} pulse send(s) this interval got no OK reply from the jig]")

    except (KeyboardInterrupt, _StopRequested):
        pass
    finally:
        jig.close()

    print("\n\n---- summary ----")
    print(f"Total pulses sent: {total_pulses_sent} (expected {total_pulses_sent / args.ppl:.4f}L at {args.ppl} ppl)")
    if readings:
        last_pulses, last_liters, last_expected = readings[-1]
        print(f"Last reading: liters_delivered={last_liters}, expected={last_expected:.4f}")
        nonzero = [r for r in readings if r[1] not in (None, 0, 0.0)]
        print(f"Readings that were nonzero: {len(nonzero)} / {len(readings)}")
        if nonzero:
            print(f"First nonzero reading was after {nonzero[0][0]} pulses sent.")
    else:
        print("No readings collected.")


if __name__ == "__main__":
    main()
