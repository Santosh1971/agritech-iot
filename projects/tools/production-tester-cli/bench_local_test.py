#!/usr/bin/env python3
"""Repeats the real guided-CLI test logic against the LOCAL, not-yet-committed
DUT firmware build -- so the WiFi-flapping fix can be validated several times
on the bench before it's pushed (which is the only way to make it available
to the actual guided CLI, since that always downloads from the backend).

Reuses cli.py's real _softap_phase()/_wifi_mqtt_handover() unmodified, just
swaps the download step for reading firmware.bin off disk.
"""
import sys
import time
sys.path.insert(0, "/Users/santoshkrjha/Projects/agritech-iot/projects/tools/production-tester-cli")

from pathlib import Path
from production_tester import flasher, ui
from production_tester.config import BenchConfig
from production_tester.hardware_discovery import find_hardware
from production_tester.api_client import Build
from production_tester.cli import (
    TestRun, _softap_phase, _wifi_mqtt_handover, capture_and_parse_boot_log, ASSETS_DIR,
)

LOCAL_FW = Path("/Users/santoshkrjha/Projects/agritech-iot/products/FG1-flowguard/firmware/.pio/build/esp32dev_ds1307/firmware.bin")


def run_local_unit() -> TestRun:
    config = BenchConfig.load()
    build = Build(id=0, product="FG1", version="local-wifi-fix", variant="esp32dev_ds1307",
                   size_bytes=LOCAL_FW.stat().st_size, notes="local bench build, not pushed")
    run = TestRun(api=None, config=config, build=build)

    hw = find_hardware()
    if hw.jig is None or hw.dut_port is None:
        print("Hardware not found -- check USB connections.")
        return run
    run.jig = hw.jig
    run.dut_port = hw.dut_port
    print(f"Jig on {hw.jig_port}, DUT on {hw.dut_port}.")
    run.jig.leave_wifi()

    mac = flasher.read_mac(run.dut_port)
    print(f"Device MAC: {mac}")
    bootloader = ASSETS_DIR / "bootloader.bin"
    partitions = ASSETS_DIR / "partitions.bin"

    print("Erasing whole chip...")
    erase = flasher.erase_chip(run.dut_port)
    if not erase.ok:
        run.set_step("flash", False, f"erase failed: {erase.detail}")
        run.jig.close()
        return run

    print("Writing local firmware...")
    flash = flasher.write_firmware(run.dut_port, bootloader, partitions, LOCAL_FW)
    if not flash.ok:
        run.set_step("flash", False, f"flash failed: {flash.detail}")
        run.jig.close()
        return run
    run.set_step("flash", True)

    parsed = capture_and_parse_boot_log(run.dut_port)
    run.device_id = parsed.device_id
    run.firmware_version = parsed.firmware_version
    run.set_step("boot_log", parsed.passed, parsed.device_id or "no device ID found in boot log")
    if not run.steps["boot_log"].passed:
        run.jig.close()
        return run

    _softap_phase(run, config)
    if run.steps["factory_reset"].passed:
        _wifi_mqtt_handover(run, config)

    run.jig.close()
    report = run.build_report()
    ui.print_summary(run.steps, report.overall_passed)
    return run


if __name__ == "__main__":
    n_runs = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    results = []
    for i in range(n_runs):
        print("\n" + "=" * 60)
        print(f"  LOCAL BENCH RUN {i + 1}/{n_runs} (unpushed WiFi-flap fix)")
        print("=" * 60)
        run = run_local_unit()
        overall = all(s.passed for s in run.steps.values())
        results.append(overall)
        if i < n_runs - 1:
            time.sleep(3)

    print("\n" + "=" * 60)
    print("  BENCH RUN SUMMARY")
    print("=" * 60)
    for i, ok in enumerate(results):
        print(f"  Run {i + 1}: {'PASS' if ok else 'FAIL'}")
