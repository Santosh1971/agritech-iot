"""
Guided CLI for FG1 production flashing + testing -- the laptop
counterpart to the Android app, built once the jig took over ALL
wireless communication with the DUT (see
docs/testing/JIG_NETWORK_BRIDGE_SPEC.md). This process never touches
WiFi itself: it only ever talks USB-serial to the jig and the DUT, and
plain HTTPS to the NB Agri Flasher backend for login/build-download/
report (which doesn't care what network it happens over). No adb, no
APK installs, no phone WiFi state to juggle -- see
PRODUCTION_TOOL_SPEC_V2.md for why that mattered.

Run with: python -m production_tester.cli
"""
from __future__ import annotations

import re
import sys
import threading
import time
from pathlib import Path
from typing import Optional

from . import dut_serial, flasher, ui
from .api_client import ApiClient, ApiError, Build
from .boot_log_parser import parse as parse_boot_log
from .config import BenchConfig
from .hardware_discovery import find_hardware
from .jig_client import JigClient
from .report import PRODUCTION_TEST_STEPS, STEP_LABELS, StepResult, TestReport, export_csv, save as save_report

ASSETS_DIR = Path(__file__).resolve().parent.parent / "assets"
BOOT_LOG_DURATION_S = 10.0
WIFI_MQTT_WAIT_S = 32.0
RESET_CONFIRM_WAIT_S = 15.0


def println_step(name: str, passed: bool, detail: str = "") -> None:
    label = STEP_LABELS.get(name, name)
    print(ui.step_line(label, passed, detail))


class TestRun:
    def __init__(self, api: ApiClient, config: BenchConfig, build: Build):
        self.api = api
        self.config = config
        self.build = build
        self.steps: dict[str, StepResult] = {name: StepResult(name) for name in PRODUCTION_TEST_STEPS}
        self.device_id: Optional[str] = None
        self.firmware_version: Optional[str] = None
        self.dut_port: Optional[str] = None
        self.jig: Optional[JigClient] = None

    def set_step(self, name: str, passed: bool, detail: str = "") -> None:
        self.steps[name] = StepResult(name, passed, detail)
        println_step(name, passed, detail)

    def build_report(self) -> TestReport:
        build_label = f"{self.build.product} {self.build.version} ({self.build.variant})"
        return TestReport(
            device_id=self.device_id, firmware_version=self.firmware_version,
            build_label=build_label, operator=self.config.operator_name,
            station=self.config.station_name, steps=self.steps,
        )


def run_one_unit(api: ApiClient, config: BenchConfig, build: Build) -> TestRun:
    run = TestRun(api, config, build)
    hw = find_hardware()
    if hw.jig is None:
        print("No jig found on any serial port -- check the USB connection and try again.")
        return run
    if hw.dut_port is None:
        print("Jig found, but no separate DUT port -- plug the DUT in too.")
        hw.jig.close()
        return run
    run.jig = hw.jig
    run.dut_port = hw.dut_port
    print(f"Jig on {hw.jig_port}, DUT on {hw.dut_port}.")

    # A jig left associated with WiFi from the previous unit's MQTT phase
    # keeps its radio actively chattering with the AP in the background --
    # 2026-09-18 bench finding: the same garbled-serial-capture symptom
    # chased for most of this session (identical text duplicated
    # thousands of times) reproduced on a completely different stack here
    # (Python/pyserial, not Android), always specifically when the jig's
    # radio was active near the DUT -- never during pure USB-only work
    # like flashing (which esptool hash-verifies clean every time). That
    # rules out either app's serial-reading code as the cause; it's most
    # likely RF/electrical interference from the jig's WiFi transmissions
    # onto the physically adjacent DUT's UART line. Starting each unit
    # with the jig's WiFi definitely off, not just assumed off, is a
    # cheap, real mitigation for that -- not a full fix (an idle STA
    # radio still does background beacon/keepalive traffic while
    # associated, hence the deliberate disconnect here rather than
    # trusting it was left in a good state).
    run.jig.leave_wifi()

    try:
        _flash_and_boot(run, api, build)
        if run.steps["flash"].passed and run.steps["boot_log"].passed:
            _softap_phase(run, config)
            _wifi_mqtt_handover(run, config)
    finally:
        run.jig.close()

    report = run.build_report()
    path = save_report(report)
    api.report_result(
        build.id, "flash_ok" if report.overall_passed else "flash_failed",
        mac=None, device_id=run.device_id,
    )
    print(f"\nReport saved: {path}")
    ui.print_summary(run.steps, report.overall_passed)
    return run


def capture_and_parse_boot_log(dut_port: str, attempts: int = 2):
    """Captures the boot log and parses it, retrying once if the parse
    failed but shows no genuine firmware error ([E] lines) -- that
    combination (missing required marker, no real error) is the signature
    of the known USB-serial read corruption (proven earlier this session
    to be an OS/driver-level artifact, not application code): a byte from
    one log line gets spliced into another (e.g. "SmartWaterController"
    and "Firmware version" merging into "Controlleersion"), so an exact
    marker substring search misses it even though the device booted fine.
    Bench-confirmed 2026-09-18: two bench runs failed "boot_log" this way
    despite Device ID printing correctly moments later in the same
    corrupted capture. A genuine boot problem reproduces on retry; a read
    glitch usually doesn't."""
    for attempt in range(attempts):
        boot_log = dut_serial.capture(dut_port, BOOT_LOG_DURATION_S)
        print(ui.header(f"---- boot log ({int(BOOT_LOG_DURATION_S)}s) ----"))
        print(dut_serial.collapse_repeats(boot_log.strip()))
        print(ui.header("---- end boot log ----"))
        parsed = parse_boot_log(boot_log)
        if parsed.passed or parsed.unexpected_errors or attempt == attempts - 1:
            return parsed
        print("Boot log check failed with no real [E] errors -- likely a USB-serial "
              "read glitch, not a real boot problem. Recapturing once...")


def _flash_and_boot(run: TestRun, api: ApiClient, build: Build) -> None:
    print(ui.header(f"---- {build.product} {build.version} ({build.variant}) ----"))
    mac = flasher.read_mac(run.dut_port)
    if mac is None:
        run.set_step("flash", False, "could not read chip MAC -- check the USB connection")
        return
    print(f"Device MAC: {mac}")

    print("Downloading firmware...")
    try:
        app_bytes = api.download_build(build.id, mac, expected_size=build.size_bytes,
                                        on_progress=lambda p: print(f"\rDownload {p}%", end="", flush=True))
        print()
    except ApiError as e:
        run.set_step("flash", False, f"download failed: {e}")
        return
    app_path = Path.home() / ".nbagri_production_tester" / "last_app.bin"
    app_path.parent.mkdir(parents=True, exist_ok=True)
    app_path.write_bytes(app_bytes)
    print(f"Downloaded {len(app_bytes) // 1024} KB.")

    bootloader = ASSETS_DIR / "bootloader.bin"
    partitions = ASSETS_DIR / "partitions.bin"
    if not bootloader.exists() or not partitions.exists():
        run.set_step("flash", False, f"missing bundled bootloader.bin/partitions.bin in {ASSETS_DIR}")
        return

    print("Erasing whole chip (clears NVS/WiFi/MQTT config too)...")
    erase = flasher.erase_chip(run.dut_port)
    if not erase.ok:
        run.set_step("flash", False, f"erase failed: {erase.detail}")
        return

    print("Writing fresh firmware...")
    flash = flasher.write_firmware(run.dut_port, bootloader, partitions, app_path)
    if not flash.ok:
        run.set_step("flash", False, f"flash failed: {flash.detail}")
        return
    run.set_step("flash", True)

    parsed = capture_and_parse_boot_log(run.dut_port)
    run.device_id = parsed.device_id
    run.firmware_version = parsed.firmware_version
    run.set_step("boot_log", parsed.passed, parsed.device_id or "no device ID found in boot log")


def _softap_phase(run: TestRun, config: BenchConfig) -> None:
    jig = run.jig
    assert jig is not None and run.device_id is not None

    if not jig.join_dut_softap(run.device_id):
        run.set_step("factory_reset", False, "jig could not join the DUT's SoftAP")
        return
    print("Jig joined the DUT's SoftAP -- SoftAP-phase commands routed through it.")

    jig.http_command("factory_reset", timeout_s=3)
    time.sleep(3)
    reconnected = False
    for _ in range(8):
        if jig.device_info() is not None:
            reconnected = True
            break
        time.sleep(1.5)
    run.set_step("factory_reset", reconnected)
    if not reconnected:
        return

    cal = jig.http_command("calibrate", {"ppl": config.expected_calibration_ppl})
    run.set_step("calibrate", cal is not None)

    jig.http_command("relay_test", timeout_s=3)
    time.sleep(0.8)
    relay_on = jig.relay_state()
    run.set_step("relay_test", relay_on is True,
                 "jig sensed relay ON" if relay_on else "jig did NOT sense relay closing")

    # liters_delivered only reflects live flow pulses while a cycle is
    # active (Scheduler::getCurrentState() -- see firmware) -- pulsing
    # with no cycle running always reads back 0 regardless of whether the
    # sensor itself is fine. Bench-confirmed 2026-09-18: 40 pulses with
    # manual_on active read back exactly 40/450=0.0889L; the same pulses
    # with no active cycle read back 0.0000L every time. This was the
    # actual cause of every previous "flow_sensor 0.00L" result.
    # manual_on's HTTP_CMD relay (SoftAP-phase) is a single request with no
    # built-in confirmation either -- bench-confirmed 2026-09-18: it can
    # silently not land (request drops, or the jig's own HTTP client
    # briefly wedged) just like the MQTT-phase commands did, producing the
    # exact same false "0.00L" as pulsing with no active cycle. Confirm
    # cycle_active before pulsing instead of assuming the single request
    # worked.
    before = None
    for _ in range(3):
        jig.http_command("manual_on")
        before = jig.device_info()
        if before and before.get("cycle_active"):
            break
        time.sleep(1.0)
    before_l = (before or {}).get("liters_delivered", 0.0)
    jig.pulse(config.flow_test_pulse_count)
    time.sleep(0.5)
    after = jig.device_info()
    after_l = (after or {}).get("liters_delivered", 0.0)
    jig.http_command("manual_off")
    delivered = after_l - before_l
    expected = config.flow_test_pulse_count / config.expected_calibration_ppl
    within = abs(delivered - expected) <= expected * 0.02
    run.set_step("flow_sensor", within, f"expected {expected:.2f}L, got {delivered:.2f}L")

    jig.http_command("rtc_sync", {"unix": int(time.time())}, timeout_s=3)
    rtc_info = jig.device_info()
    run.set_step("rtc", bool((rtc_info or {}).get("rtc_set")), f"rtc_time={(rtc_info or {}).get('rtc_time', '')}")

    if not config.office_wifi_ssid:
        run.set_step("wifi_mqtt", False, "no office WiFi SSID set -- run with --settings first")
        for name in ("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt"):
            run.set_step(name, False, "skipped -- WiFi/MQTT never connected")
        reset_ack = jig.http_command("factory_reset", timeout_s=5)
        run.set_step("ship_clean_reset", reset_ack is not None, "sent via SoftAP-phase transport")
        run.set_step("factory_reset_confirmed", False, "skipped -- see wifi_mqtt")
        return

    jig.http_command("wifi_config", {"ssid": config.office_wifi_ssid, "pass": config.office_wifi_password})
    jig.http_command("resume_auto_mode", timeout_s=3)
    jig.leave_wifi()


def _wifi_mqtt_handover(run: TestRun, config: BenchConfig) -> None:
    jig = run.jig
    assert jig is not None and run.device_id is not None
    if run.steps["wifi_mqtt"].passed is False and run.steps["wifi_mqtt"].detail.startswith("no office"):
        return  # already handled (no SSID configured) in _softap_phase

    print(f"Waiting up to {int(WIFI_MQTT_WAIT_S)}s for the DUT to reach the office WiFi + broker...")
    live_log = dut_serial.capture(run.dut_port, WIFI_MQTT_WAIT_S)
    print(ui.header("---- DUT serial during WiFi/MQTT wait ----"))
    print(dut_serial.collapse_repeats(live_log.strip()))
    print(ui.header("---- end ----"))

    wifi_ok = "[WiFi] Connected" in live_log or "[WiFi] Reconnected" in live_log
    mqtt_ok = "[MQTT] Connected" in live_log
    passed = wifi_ok and mqtt_ok
    mqtt_fail = re.search(r"\[MQTT\] Failed rc=-?\d+.*", live_log)
    if passed:
        detail = "confirmed via DUT serial (WiFi + MQTT connected lines seen)"
    elif mqtt_fail:
        detail = mqtt_fail.group(0)
    elif not wifi_ok:
        detail = "no \"[WiFi] Connected\"/\"[WiFi] Reconnected\" line seen -- check the office router is reachable"
    else:
        detail = "WiFi connected but no \"[MQTT] Connected\" line seen"

    mqtt_bridge_ok = False
    if passed:
        # The jig left the DUT's SoftAP at the end of _softap_phase(); now
        # join the same office WiFi the DUT itself just proved works, and
        # bridge MQTT over it -- this is what replaces the phone's flaky
        # cellular-bound MqttCommander entirely (see the "why phone
        # connects to broker" discussion this was built to resolve).
        joined_office = jig.join_ap_with_poll(config.office_wifi_ssid, config.office_wifi_password)
        if joined_office:
            mqtt_bridge_ok = jig.mqtt_connect(run.device_id)
        if mqtt_bridge_ok:
            # Retried, not a single check -- the DUT only publishes every
            # ~5s (STATUS_PUBLISH_INTERVAL_MS), and this office network's
            # own WiFi flapping (seen on every run this session) means a
            # single window can easily land between two publishes. 10s
            # covers at least one full interval with margin.
            status = jig._poll_status(10.0)
            detail += "; broker confirms status message received" if status else \
                "; broker check: no status message seen in 10s (may need network/ACL review)"
        elif not joined_office:
            detail += "; jig could not join the office WiFi"
        else:
            detail += "; jig could not reach the broker over office WiFi"
    run.set_step("wifi_mqtt", passed, detail)

    if not (passed and mqtt_bridge_ok):
        for name in ("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt"):
            reason = "DUT's WiFi/MQTT is fine, but the jig couldn't reach the broker" if passed \
                else "WiFi/MQTT never connected"
            run.set_step(name, False, f"skipped -- {reason}")
        if mqtt_bridge_ok:
            _ship_clean_reset(run, jig)
        else:
            run.set_step("ship_clean_reset", False, "skipped -- broker unreachable")
            run.set_step("factory_reset_confirmed", False, "skipped -- see wifi_mqtt")
        return

    # Settle delay before the first MQTT command in this phase. Bench
    # finding 2026-09-18: relay_test/manual_on, sent immediately after
    # this handover confirms, never once arrived at the DUT (confirmed by
    # the total absence of the DUT's own unconditional "[MQTT] Received
    # on..." log line -- not a dispatch or status-read bug, a real
    # non-delivery). factory_reset, sent later in this same function after
    # ~30s more elapsed time (relay/flow/rtc attempts + polls), landed
    # every time. The connection is least stable in the seconds right after
    # WiFi/MQTT first comes up (matches the ASSOC_LEAVE/DNS-fail/rc=-2
    # churn seen throughout this session) -- waiting it out here, instead
    # of accidentally getting it for free via unrelated steps, should let
    # relay_test/manual_on land on the first attempt too.
    print("Letting the WiFi/MQTT connection settle before sending commands...")
    time.sleep(20.0)

    # A single mqtt_command() only confirms the jig published -- not that
    # the DUT received it (QoS0, no session/ack on either PubSubClient).
    # relay_test/manual_on were the only two commands in this phase still
    # using the bare fire-and-forget publish, and were the only two steps
    # that failed consistently across repeated bench runs 2026-09-18 even
    # after the DUT-side WiFi-flap bug was fixed -- ship_clean_reset (which
    # already used mqtt_command_with_retry) passed reliably in the same
    # runs. Switching these two to the same retry-and-confirm helper.
    relay_landed, _ = _publish_confirmed_by_serial(run, jig, "relay_test")
    if relay_landed:
        time.sleep(0.8)
        relay_on = jig.relay_state()
        run.set_step("relay_test_mqtt", relay_on is True,
                     "jig sensed relay ON (via MQTT)" if relay_on else "jig did NOT sense relay closing")
    else:
        run.set_step("relay_test_mqtt", False, "relay_test never confirmed landing on DUT serial")

    manual_on_landed, _ = _publish_confirmed_by_serial(run, jig, "manual_on")
    if manual_on_landed:
        before = jig.mqtt_status() or {}
        jig.pulse(config.flow_test_pulse_count)
        after = jig._poll_status(7.0) or before
        before_l, after_l = before.get("liters_delivered", 0.0), after.get("liters_delivered", 0.0)
        delivered = after_l - before_l
        expected = config.flow_test_pulse_count / config.expected_calibration_ppl
        within = abs(delivered - expected) <= expected * 0.02
        run.set_step("flow_sensor_mqtt", within, f"expected {expected:.2f}L, got {delivered:.2f}L (via MQTT)")
        jig.mqtt_command_with_retry("manual_off", confirmed_by=lambda s: s.get("cycle_active") is False)
    else:
        run.set_step("flow_sensor_mqtt", False, "manual_on never confirmed landing on DUT serial")

    # 7s used to occasionally miss: the DUT only publishes every
    # STATUS_PUBLISH_INTERVAL_MS=5s, and mqtt_status() drains whatever it
    # last saw -- if manual_off's own confirm poll just consumed the
    # latest message, this poll can need most of a full next interval.
    # 10s covers that with margin.
    rtc_status = jig._poll_status(10.0)
    run.set_step("rtc_mqtt", bool((rtc_status or {}).get("rtc_set")), f"rtc_time={(rtc_status or {}).get('rtc_time', '')}")

    _ship_clean_reset(run, jig)


def _publish_confirmed_by_serial(
    run: TestRun, jig: JigClient, cmd: str, extra: Optional[dict] = None,
    wait_s: float = 6.0, retries: int = 2,
) -> tuple[bool, str]:
    """Publishes [cmd] via MQTT and confirms it landed by watching the
    DUT's own serial for "[CMD] {cmd}" -- the same proven-reliable
    pattern _ship_clean_reset() uses for factory_reset, applied to
    relay_test/manual_on. mqtt_status()/_poll_status() confirmation (a
    round trip through the jig's own MQTT subscription) was unreliable
    for these two commands across every bench run 2026-09-18 even after
    adding retries, while watching the DUT's serial directly -- which
    factory_reset already relied on -- passed reliably in the same runs.
    Returns (landed, full captured log)."""
    captured: dict[str, str] = {}

    def _do_capture() -> None:
        captured["log"] = dut_serial.capture(run.dut_port, wait_s)

    capture_thread = threading.Thread(target=_do_capture)
    capture_thread.start()
    time.sleep(1.0)
    jig.mqtt_command(cmd, extra)
    capture_thread.join()
    full_log = captured.get("log", "")

    marker = f"[CMD] {cmd}"
    for _ in range(retries):
        if marker in full_log:
            return True, full_log
        jig.mqtt_command(cmd, extra)
        full_log += dut_serial.capture(run.dut_port, wait_s / 2)
    return marker in full_log, full_log


def _ship_clean_reset(run: TestRun, jig: JigClient) -> None:
    """Sends factory_reset via MQTT and confirms it from the DUT's own
    serial output, captured *starting before* the command is published --
    not from a follow-up MQTT status poll.

    2026-09-18 bench finding, confirmed with a clean isolated test: a
    single factory_reset publish lands and is processed reliably (DUT
    logs "[MQTT] Received...", "[NVS] Factory reset done", then reboots)
    every time. The bug was never delivery -- it was that a successful
    reset wipes the DUT's saved WiFi credentials and it reboots straight
    into its own SoftAP within a couple of seconds, dropping off the
    broker for good that session. Everything the old code did *after*
    sending the command (2 more factory_reset publishes, a
    force_local_mode nothing on the network could still deliver, then
    polling for a status update) was reading either a stale pre-reboot
    status or nothing at all -- reporting a reset that fully succeeded as
    a failure, every single time. Watching the DUT's own serial
    concurrently with the publish catches the real evidence directly,
    the same way the boot-log capture after flashing already does.
    """
    print("Sending ship-clean factory_reset (watching DUT serial concurrently)...")
    captured: dict[str, str] = {}

    def _do_capture() -> None:
        captured["log"] = dut_serial.capture(run.dut_port, RESET_CONFIRM_WAIT_S)

    capture_thread = threading.Thread(target=_do_capture)
    capture_thread.start()
    time.sleep(1.0)  # let the capture thread's port genuinely open before publishing
    jig.mqtt_command("factory_reset")
    capture_thread.join()
    reset_log = captured.get("log", "")

    if not _reset_landed(reset_log):
        # Genuinely didn't land yet (or landed just outside the window) --
        # retry a few times, then fall back to force_local_mode as a last
        # resort so the unit at least leaves office WiFi even if NVS
        # doesn't end up confirmed blank.
        print("No evidence factory_reset landed yet -- retrying...")
        for _ in range(2):
            jig.mqtt_command("factory_reset")
            extra = dut_serial.capture(run.dut_port, 5.0)
            reset_log += extra
            if _reset_landed(extra):
                break
        if not _reset_landed(reset_log):
            local_status = jig.mqtt_command_with_retry(
                "force_local_mode", attempts=3, interval_s=2.5,
                confirmed_by=lambda s: not s.get("wifi_connected"),
            )
            off_wifi = not (local_status or {}).get("wifi_connected", True)
            run.set_step(
                "ship_clean_reset", off_wifi,
                "factory_reset never confirmed landing; force_local_mode sent as backup -- "
                + ("DUT off office WiFi" if off_wifi else "still shows wifi_connected=true"),
            )
            _confirm_reset_from_log(run, jig, reset_log)
            return

    run.set_step("ship_clean_reset", True, "confirmed via DUT serial")
    _confirm_reset_from_log(run, jig, reset_log)


def _reset_landed(log: str) -> bool:
    """True if there's evidence the DUT received and acted on factory_reset
    -- broader than just "[NVS] Factory reset done", since that specific
    line has a real chance of falling into a corrupted stretch of the
    capture (the known USB-driver-level byte-duplication artifact, see
    dut_serial.collapse_repeats()'s doc comment) even when the command
    demonstrably landed. 2026-09-18 bench finding: a run showed
    "[MQTT] Received .../command: {\"cmd\": \"factory_reset\"}" and
    "[CMD] factory_reset" clearly, immediately followed by a large
    corrupted/empty stretch swallowing "Factory reset done" specifically
    -- yet the full reboot-into-blank-SoftAP evidence was right there
    moments later in the same capture. Any one of these markers is
    sufficient; the later ones are strictly stronger evidence than the
    earlier ones (they can only appear after a real reboot happened)."""
    return any(marker in log for marker in (
        "Factory reset done", "[CMD] factory_reset",
        "wifi_ssid NOT_FOUND", "starting local fallback (SoftAP)",
    ))


def _confirm_reset_from_log(run: TestRun, jig: JigClient, reset_log: str) -> None:
    print(ui.header("---- DUT serial during ship-clean reset ----"))
    print(dut_serial.collapse_repeats(reset_log.strip()))
    print(ui.header("---- end ----"))

    nvs_blank = "wifi_ssid NOT_FOUND" in reset_log
    serial_confirms = nvs_blank or "starting local fallback (SoftAP)" in reset_log
    if serial_confirms:
        detail = "confirmed genuinely blank -- boot log shows wifi_ssid NOT_FOUND" if nvs_blank \
            else "confirmed via DUT serial (\"starting local fallback (SoftAP)\")"
        run.set_step("factory_reset_confirmed", True, detail)
        return

    # The reboot may just not have finished printing that far within the
    # capture window -- give it a moment and check by rejoining its
    # SoftAP directly instead of declaring failure immediately.
    print("Rejoining the DUT's SoftAP to confirm the reset...")
    time.sleep(2)
    assert run.device_id is not None
    joined = jig.join_dut_softap(run.device_id)
    ws_confirms = False
    if joined:
        info = jig.device_info(timeout_s=8)
        ws_confirms = bool(info) and not info.get("wifi_connected", True)
    if ws_confirms:
        run.set_step(
            "factory_reset_confirmed", False,
            "WiFi is off (via jig) but no reboot/blank-NVS evidence seen in the capture window -- "
            "likely fine, just slow to reappear; treat as unconfirmed",
        )
    else:
        run.set_step("factory_reset_confirmed", False, "could not confirm -- unit may not have actually reset")


def _prompt_settings(config: BenchConfig) -> BenchConfig:
    print("\n-- Bench settings (blank keeps current value) --")
    ssid = input(f"Office WiFi SSID [{config.office_wifi_ssid}]: ").strip()
    if ssid:
        config.office_wifi_ssid = ssid
    pw = input("Office WiFi password [unchanged]: ").strip()
    if pw:
        config.office_wifi_password = pw
    operator = input(f"Operator name [{config.operator_name}]: ").strip()
    if operator:
        config.operator_name = operator
    station = input(f"Station name [{config.station_name}]: ").strip()
    if station:
        config.station_name = station
    config.save()
    return config


def _login(api: ApiClient) -> None:
    if api.is_logged_in:
        return
    email = input("Email: ").strip()
    api.request_otp(email)
    code = input("Code sent -- enter it: ").strip()
    api.verify_otp(email, code)
    print("Logged in.")


def _prompt_choice(prompt: str, options: list[str]) -> int:
    """Prompts until given a valid choice -- either the 1-based number
    shown, or (case-insensitively) the option text itself, since typing
    the name back (e.g. "FG1") is a natural response to a list that just
    displayed it. Loops on anything else instead of crashing with a
    traceback -- 2026-09-18 bench finding: int(input(...)) blew up the
    whole run on the very first real (non-scripted) use."""
    while True:
        raw = input(prompt).strip()
        if raw.isdigit() and 1 <= int(raw) <= len(options):
            return int(raw) - 1
        for i, opt in enumerate(options):
            if raw.lower() == opt.lower():
                return i
        print(f"  Enter a number from 1-{len(options)}, or the name shown.")


def _pick_build(api: ApiClient) -> Build:
    grant = api.fetch_grant()
    product = grant.products[0] if len(grant.products) == 1 else None
    if product is None:
        for i, p in enumerate(grant.products):
            print(f"  {i + 1}) {p}")
        idx = _prompt_choice("Product #: ", grant.products)
        product = grant.products[idx]
    builds = api.fetch_builds(product)
    build_labels = []
    for i, b in enumerate(builds):
        label = f"{b.product} {b.version} ({b.variant}) -- {b.size_bytes // 1024} KB" + (f" -- {b.notes}" if b.notes else "")
        build_labels.append(f"{b.product} {b.version}")
        print(f"  {i + 1}) {label}")
    idx = _prompt_choice("Build #: ", build_labels)
    return builds[idx]


def main() -> None:
    config = BenchConfig.load()
    if "--settings" in sys.argv or not config.office_wifi_ssid:
        config = _prompt_settings(config)

    api = ApiClient()
    _login(api)
    build = _pick_build(api)

    while True:
        print("\n" + "=" * 60)
        input("Plug in the next unit (DUT + jig on USB), then press Enter...")
        run_one_unit(api, config, build)
        again = input("\nTest another unit? [Y/n] ").strip().lower()
        if again == "n":
            break

    csv_path = export_csv()
    print(f"\nAll results exported to {csv_path}")


if __name__ == "__main__":
    main()
