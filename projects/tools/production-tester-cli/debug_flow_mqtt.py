#!/usr/bin/env python3
"""
Standalone, tight repeat-loop diagnostic for the "flow sensor via MQTT"
bench finding (2026-09-19): with cycle_active explicitly confirmed true
right before pulsing, delivered liters still occasionally comes back
0.00L -- but ONLY during the MQTT phase (jig's own WiFi radio actively
bridging), never during the SoftAP phase (jig radio idle) using the
exact same pulse-sending code. This isolates JUST that one test in a
fast loop, with the DUT's and jig's live serial printed continuously to
the terminal, so a human (and the next debugging pass) can watch exactly
what's happening around a failure instead of waiting through a full ~2
minute production run each time.

Does ONE real SoftAP join + WiFi handover + MQTT connect up front (using
the bench's saved office WiFi from ~/.nbagri_production_tester/config.json),
then loops manual_on -> pulse -> check -> manual_off indefinitely WITHOUT
ever resetting the DUT in between -- no factory_reset, no ship_clean_reset
-- so each iteration is fast and directly comparable to the last.

Setup: DUT already flashed and sitting on its own SoftAP (power it on, or
run the main tool once and stop before it factory-resets at the end).

Run:
    python3 debug_flow_mqtt.py --device-id SWC_001_B468
    python3 debug_flow_mqtt.py --device-id SWC_001_B468 --iterations 10 --pulse-count 100

Ctrl+C to stop -- prints a pass/fail summary either way.
"""
from __future__ import annotations

import argparse
import re
import threading
import time

from production_tester import dut_serial
from production_tester.config import BenchConfig
from production_tester.hardware_discovery import find_hardware


class _Stop(Exception):
    pass


def _tagged_printer(tag: str):
    """Returns an on_line callback that prints [tag] lines with a lock so
    the two live streams (DUT + jig) don't interleave garbled mid-line on
    a shared stdout."""
    def _on_line(text: str) -> None:
        with _print_lock:
            print(f"{tag} {text}")
    return _on_line


_print_lock = threading.Lock()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--device-id", required=True, help="DUT's SoftAP name, e.g. SWC_001_B468")
    parser.add_argument("--iterations", type=int, default=0, help="Stop after N iterations (default: run until Ctrl+C)")
    parser.add_argument("--pulse-count", type=int, default=None, help="Overrides the bench config's flow_test_pulse_count")
    parser.add_argument("--rate-hz", type=float, default=4.0, help="Pulse rate (default: 4 Hz, matches the production test)")
    parser.add_argument("--ppl", type=int, default=None, help="Overrides the bench config's expected_calibration_ppl")
    args = parser.parse_args()

    config = BenchConfig.load()
    pulse_count = args.pulse_count or config.flow_test_pulse_count
    ppl = args.ppl or config.expected_calibration_ppl
    expected = pulse_count / ppl

    print("Finding hardware...")
    hw = find_hardware(on_jig_line=_tagged_printer("JIG>"))
    if hw.jig is None:
        print("No jig found -- check the USB connection.")
        return
    if hw.dut_port is None:
        print("Jig found, but no separate DUT port -- plug the DUT in too.")
        hw.jig.close()
        return
    jig = hw.jig
    dut_port = hw.dut_port
    print(f"Jig on {hw.jig_port}, DUT on {dut_port}.")

    # Live DUT tail -- printed continuously for the whole script's life,
    # same tail() used by the web UI's live console, just aimed at stdout
    # here instead of an SSE event. Also watches for the DUT's own LAN IP
    # (printed on WiFi reconnect) so this script can use the same LAN
    # HTTP verification path the main flow does -- that's specifically
    # the path that's been showing the 0.00L failures on the bench.
    dut_ip: dict[str, str] = {}
    ip_re = re.compile(r"IP:\s*(\d{1,3}(?:\.\d{1,3}){3})")

    def on_dut_line(text: str) -> None:
        with _print_lock:
            print(f"DUT> {text}")
        m = ip_re.search(text)
        if m:
            dut_ip["ip"] = m.group(1)

    stop_event = threading.Event()
    tail_thread = threading.Thread(target=dut_serial.tail, args=(dut_port, on_dut_line, stop_event), daemon=True)
    tail_thread.start()

    try:
        print(f"Joining DUT's SoftAP as {args.device_id}...")
        if not jig.join_dut_softap(args.device_id):
            print("Could not join the DUT's SoftAP -- check it's powered on and broadcasting.")
            return
        print("Joined. Handing over to office WiFi...")

        wifi_cfg_ok = None
        for _ in range(3):
            wifi_cfg_ok = jig.http_command("wifi_config", {"ssid": config.office_wifi_ssid, "pass": config.office_wifi_password})
            if wifi_cfg_ok is not None:
                break
            time.sleep(1.0)
        if wifi_cfg_ok is None:
            print("wifi_config never reached the DUT over SoftAP -- aborting.")
            return
        jig.http_command("resume_auto_mode", timeout_s=3)
        jig.leave_wifi()

        print(f"Waiting for the DUT to settle on \"{config.office_wifi_ssid}\"...")
        time.sleep(35.0)  # DUT-side WIFI_RETRY + stability window; watch the DUT> lines above for real progress

        print(f"Jig joining \"{config.office_wifi_ssid}\"...")
        if not jig.join_ap_with_poll(config.office_wifi_ssid, config.office_wifi_password):
            print("Jig could not join the office WiFi -- aborting.")
            return
        print("Jig connecting to the broker...")
        if not jig.mqtt_connect(args.device_id):
            print("Jig could not connect to the broker -- aborting.")
            return
        print(f"Connected. DUT LAN IP seen so far: {dut_ip.get('ip', '(none yet -- will fall back to MQTT status)')}")

        passed = 0
        failed = 0
        i = 0
        while args.iterations == 0 or i < args.iterations:
            i += 1
            print(f"\n==== iteration {i} ====")

            ip = dut_ip.get("ip")
            cycle_confirmed = False
            before: dict = {}
            for attempt in range(5):
                if ip:
                    jig.lan_http_command(ip, "manual_on")
                    before = jig.lan_device_info(ip) or {}
                else:
                    jig.mqtt_command("manual_on")
                    before = jig._poll_status(3.5) or {}
                print(f"  manual_on attempt {attempt + 1}: cycle_active={before.get('cycle_active')} "
                      f"liters_delivered={before.get('liters_delivered')}")
                if before.get("cycle_active"):
                    cycle_confirmed = True
                    break
                if ip:
                    time.sleep(1.0)
            if not cycle_confirmed:
                print("  FAIL -- manual_on never confirmed active.")
                failed += 1
                continue

            interval_s = 1.0 / args.rate_hz
            next_at = time.monotonic()
            no_reply_count = 0
            for p in range(pulse_count):
                ok = jig.pulse(1)
                if not ok:
                    no_reply_count += 1
                    print(f"  [pulse {p + 1}/{pulse_count}: NO OK REPLY FROM JIG]")
                next_at += interval_s
                sleep_for = next_at - time.monotonic()
                if sleep_for > 0:
                    time.sleep(sleep_for)
            if no_reply_count:
                print(f"  {no_reply_count}/{pulse_count} pulses got no OK reply from the jig this iteration.")

            after = None
            for _ in range(3):
                after = jig.lan_device_info(ip) if ip else jig._poll_status(4.0)
                if after is not None:
                    break
                print("  [after-read failed, retrying...]")
                time.sleep(1.0)
            if after is None:
                print("  UNCONFIRMED -- pulses sent and acked, but could not read status after (retried 3x).")
                failed += 1
                if ip:
                    jig.lan_http_command(ip, "manual_off")
                else:
                    jig.mqtt_command_with_retry("manual_off", confirmed_by=lambda s: s.get("cycle_active") is False)
                time.sleep(2.0)
                continue
            before_l = before.get("liters_delivered", 0.0) or 0.0
            after_l = after.get("liters_delivered", 0.0) or 0.0
            delivered = after_l - before_l
            within = abs(delivered - expected) <= expected * 0.02
            status = "PASS" if within else "FAIL"
            print(f"  {status} -- expected {expected:.2f}L, got {delivered:.2f}L "
                  f"(before={before_l:.4f} after={after_l:.4f}, no-reply pulses={no_reply_count})")
            if within:
                passed += 1
            else:
                failed += 1

            if ip:
                jig.lan_http_command(ip, "manual_off")
            else:
                jig.mqtt_command_with_retry("manual_off", confirmed_by=lambda s: s.get("cycle_active") is False)
            time.sleep(2.0)

    except (KeyboardInterrupt, _Stop):
        pass
    finally:
        stop_event.set()
        tail_thread.join(timeout=2.0)
        jig.close()

    print(f"\n---- summary ----\n{passed} passed, {failed} failed")


if __name__ == "__main__":
    main()
