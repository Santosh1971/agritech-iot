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

import calendar
import re
import sys
import threading
import time
from datetime import datetime
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


class StopRequested(Exception):
    """Raised via TestRun.check_stop() to unwind out of run_one_unit()
    cleanly once the operator asks for a mid-run stop -- see the web UI's
    Stop button / RunManager.stop_event. Deliberately a plain exception
    (not a signal or thread-kill) so it only ever interrupts between
    already-safe boundaries (checked explicitly at phase/step/loop
    boundaries below), never mid-flash or mid-serial-write."""


class TestRun:
    def __init__(self, api: ApiClient, config: BenchConfig, build: Build, on_step=None,
                 stop_event: Optional[threading.Event] = None):
        self.api = api
        self.config = config
        self.build = build
        self.steps: dict[str, StepResult] = {name: StepResult(name) for name in PRODUCTION_TEST_STEPS}
        self.device_id: Optional[str] = None
        self.firmware_version: Optional[str] = None
        self.dut_port: Optional[str] = None
        self.jig: Optional[JigClient] = None
        # The DUT's IP on the office WiFi/hotspot LAN, once known -- lets
        # relay_test_mqtt/flow_sensor_mqtt/rtc_mqtt verify results via a
        # direct HTTP request over that shared LAN (LAN_HTTP_CMD) instead
        # of only through the jig's own flaky MQTT status subscription.
        self.dut_lan_ip: Optional[str] = None
        # Set (only when a live DUT console is wanted -- see
        # run_one_unit()'s on_dut_line) so every dut_serial.capture()
        # call below can pause the background tail() reader instead of
        # racing it for the same port.
        self.dut_tail_coordinator: Optional[dut_serial.TailCoordinator] = None
        # Structured hook alongside the printed line -- the web UI uses this
        # to update its step checklist live instead of re-parsing terminal
        # text. None (the default) keeps the plain-CLI behavior unchanged.
        self.on_step = on_step
        # 2026-09-19: operator-facing "Stop" control -- the web UI sets
        # this Event from a Stop button; check_stop() is called at phase/
        # step/loop boundaries throughout so a stop takes effect promptly
        # without ever interrupting mid-flash or mid-write.
        self.stop_event = stop_event
        self.stopped = False

    def check_stop(self) -> None:
        if self.stop_event is not None and self.stop_event.is_set():
            raise StopRequested()

    def set_step(self, name: str, passed: bool, detail: str = "") -> None:
        self.steps[name] = StepResult(name, passed, detail)
        println_step(name, passed, detail)
        if self.on_step is not None:
            self.on_step(name, passed, detail)

    def build_report(self) -> TestReport:
        build_label = f"{self.build.product} {self.build.version} ({self.build.variant})"
        return TestReport(
            device_id=self.device_id, firmware_version=self.firmware_version,
            build_label=build_label, operator=self.config.operator_name,
            station=self.config.station_name, steps=self.steps,
        )


def run_one_unit(
    api: ApiClient, config: BenchConfig, build: Build, on_step=None,
    skip_flash: bool = False, manual_device_id: Optional[str] = None,
    on_jig_line=None, on_dut_line=None,
    stop_event: Optional[threading.Event] = None,
) -> TestRun:
    run = TestRun(api, config, build, on_step=on_step, stop_event=stop_event)
    hw = find_hardware(on_jig_line=on_jig_line)
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

    # Best-effort live DUT console for the web UI -- tails the DUT's port
    # continuously in the background so a viewer can watch it happen
    # instead of only seeing the specific snapshots the test itself
    # captures (boot log, WiFi/MQTT wait, reset confirm). Every one of
    # those capture() calls below pauses this tailer first (via
    # run.dut_tail_coordinator) instead of racing it for the same port --
    # bench-confirmed 2026-09-18: two handles briefly open to the same
    # port at once can crash a read with an OSError that isn't a plain
    # "port busy" exception, not just silently lose a few bytes.
    # Flashing (a separate esptool subprocess, not just another handle in
    # this process) still gets a clean SerialException either way, which
    # the tailer's own retry loop already handles without needing the
    # coordinator paused for it specifically.
    dut_tail_stop = threading.Event()
    dut_tail_thread = None
    if on_dut_line is not None:
        run.dut_tail_coordinator = dut_serial.TailCoordinator()
        dut_tail_thread = threading.Thread(
            target=dut_serial.tail,
            args=(run.dut_port, on_dut_line, dut_tail_stop),
            kwargs={"coordinator": run.dut_tail_coordinator},
            daemon=True,
        )
        dut_tail_thread.start()

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
        run.check_stop()
        if skip_flash:
            # Debug-loop mode: reuse whatever's already on the DUT
            # instead of the usual erase+write+reboot cycle -- 2026-09-18,
            # added specifically so relay_test_mqtt/flow_sensor_mqtt could
            # be iterated on in under a minute instead of ~4. Nothing
            # here is assumed: _run_test_phases() below still does a real
            # jig.join_dut_softap(device_id) as its very first action,
            # which is a genuine live check that the DUT is reachable and
            # actually in SoftAP mode under this device ID, not flashed
            # firmware skipping past a check that would normally catch a
            # DUT that's off, mid-boot, or already joined to a router.
            print(f"Skipping flash -- reusing already-flashed unit as {manual_device_id!r}.")
            run.device_id = manual_device_id
            run.set_step("flash", True, "skipped -- reusing already-flashed unit")
            run.set_step("boot_log", True, "skipped -- reusing already-flashed unit")
        else:
            _flash_and_boot(run, api, build)
        run.check_stop()
        if run.steps["flash"].passed and run.steps["boot_log"].passed:
            _run_test_phases(run, config)
    except StopRequested:
        run.stopped = True
        print("\nTest stopped by operator.")
    finally:
        run.jig.close()
        dut_tail_stop.set()
        if dut_tail_thread is not None:
            dut_tail_thread.join(timeout=2.0)

    report = run.build_report()
    path = save_report(report)
    if run.stopped:
        print("Skipping backend report -- test was stopped by operator before completing.")
    else:
        api.report_result(
            build.id, "flash_ok" if report.overall_passed else "flash_failed",
            mac=None, device_id=run.device_id,
        )
    print(f"\nReport saved: {path}")
    ui.print_summary(run.steps, report.overall_passed)
    return run


def _pulse_visibly(jig: JigClient, count: int, rate_hz: float = 4.0, run: Optional["TestRun"] = None) -> None:
    """Sends [count] pulses one at a time at [rate_hz] instead of one
    PULSE:<count> burst -- 2026-09-18: the operator couldn't see any
    pulse/flow-LED activity during the burst (too fast to perceive) and
    asked to slow it down enough to watch it happen, not just trust the
    final liters_delivered number. 4Hz is still clearly visible (2Hz was
    bench-proven visible via flow_diagnostic.py's free-run mode; this is
    just faster per an explicit follow-up ask) while roughly halving how
    long the whole pulse train takes.

    Stops any active SQUARE:<hz> wave first -- bench-confirmed 2026-09-18:
    spacing pulses out like this (vs. one blocking burst) gives loop()
    idle time between them, and a square wave left running on the same
    pin keeps toggling it in those gaps, injecting extra edges the DUT's
    flow ISR counts as real pulses (~3% overcount seen on the bench)."""
    jig.stop_square()
    interval_s = 1.0 / rate_hz
    next_at = time.monotonic()
    for _ in range(count):
        if run is not None:
            run.check_stop()
        jig.pulse(1)
        next_at += interval_s
        sleep_for = next_at - time.monotonic()
        if sleep_for > 0:
            time.sleep(sleep_for)


def capture_and_parse_boot_log(dut_port: str, attempts: int = 2, coordinator=None):
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
        boot_log = dut_serial.capture(dut_port, BOOT_LOG_DURATION_S, coordinator=coordinator)
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
    # 2026-09-19 bench finding: the live DUT tail thread starts (see
    # run_one_unit()) well before this function runs, and every
    # dut_serial.capture() call already pauses it via the coordinator --
    # but read_mac()/erase_chip()/write_firmware() below are separate
    # esptool *subprocesses*, not another capture() call, so they were
    # never covered by that same guard. First real (non-skip-flash) run
    # through the web UI since the live panes went in failed immediately
    # with "could not read chip MAC" -- the tail thread's own open
    # pyserial handle was still holding the DUT port when esptool tried
    # to open it for the MAC read, the same two-handles-on-one-port
    # conflict already root-caused once this session, just on a new path.
    # Pause for this whole function (not just the esptool calls) and let
    # the final capture_and_parse_boot_log() call's own resume hand it
    # back -- consistent with every other coordinator use in this file.
    if run.dut_tail_coordinator is not None:
        run.dut_tail_coordinator.pause()
        time.sleep(dut_serial.TAIL_SETTLE_S)
    try:
        mac = flasher.read_mac(run.dut_port)
        if mac is None:
            run.set_step("flash", False, "could not read chip MAC -- check the USB connection")
            return
        print(f"Device MAC: {mac}")

        run.check_stop()
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

        run.check_stop()
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
    finally:
        if run.dut_tail_coordinator is not None:
            run.dut_tail_coordinator.resume()

    run.check_stop()
    parsed = capture_and_parse_boot_log(run.dut_port, coordinator=run.dut_tail_coordinator)
    run.device_id = parsed.device_id
    run.firmware_version = parsed.firmware_version
    run.set_step("boot_log", parsed.passed, parsed.device_id or "no device ID found in boot log")


def _mark_remaining_skipped(run: TestRun, reason: str) -> None:
    """Marks every not-yet-run step False with [reason] -- used when the
    unit turns out to be unreachable by either transport (SoftAP AND
    office WiFi/MQTT) so the report doesn't just leave them at their
    default not-run (None) state."""
    for name in ("calibrate", "relay_test", "flow_sensor", "rtc", "wifi_mqtt",
                 "relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt",
                 "ship_clean_reset", "factory_reset_confirmed"):
        run.set_step(name, False, f"skipped -- {reason}")


def _run_test_phases(run: TestRun, config: BenchConfig) -> None:
    """Top-level test orchestration, once flash+boot_log are known good.

    Normal case: the DUT is freshly flashed/reset and sitting in its own
    SoftAP -- join it, run the local (SoftAP-phase) tests, hand it over to
    the office WiFi, then run the MQTT-phase tests.

    2026-09-19 addition: if the jig can't join the DUT's SoftAP at all,
    that doesn't necessarily mean the DUT is off or dead -- it may simply
    already be provisioned and sitting on the office WiFi (a unit being
    re-run, or one that never dropped its existing WiFi config). Rather
    than fail the whole unit immediately, check for that over MQTT first:
    if it responds, run the MQTT-phase tests against it as found, then
    force it back into SoftAP (and confirm it actually left the broker)
    before falling through to the same local SoftAP-phase tests -- this
    is the order the operator explicitly asked for, matching how a
    field-return unit would actually need to be handled."""
    jig = run.jig
    assert jig is not None and run.device_id is not None

    run.check_stop()
    if jig.join_dut_softap(run.device_id):
        print("Jig joined the DUT's SoftAP -- SoftAP-phase commands routed through it.")
        _softap_local_tests(run, config)
        run.check_stop()
        if run.steps["factory_reset"].passed:
            _provision_office_wifi(run, config)
            run.check_stop()
            _wifi_mqtt_handover(run, config)
        return

    _mqtt_first_fallback(run, config)


def _probe_dut_via_mqtt(run: TestRun, config: BenchConfig) -> bool:
    """Checks whether the DUT is already alive on the office WiFi/broker
    instead of assuming "SoftAP unreachable" means "off or dead" -- joins
    the office WiFi, connects+subscribes over MQTT, and waits for a real
    status message from this specific device_id before concluding it's
    there. Leaves the jig on office WiFi/MQTT on success (the caller's
    MQTT-phase tests run immediately after); leaves it wherever join
    attempts left it on failure (caller decides what to do next)."""
    jig = run.jig
    assert jig is not None and run.device_id is not None
    print(f"Joining office WiFi \"{config.office_wifi_ssid}\" to check for the DUT over MQTT...")
    if not jig.join_ap_with_poll(config.office_wifi_ssid, config.office_wifi_password):
        print("Jig could not join the office WiFi either.")
        return False
    if not jig.mqtt_connect(run.device_id):
        print("Jig joined the office WiFi but could not connect to the broker.")
        return False
    status = jig._poll_status(10.0)
    if status is None:
        print("Connected to the broker but saw no status message from this device in 10s.")
        return False
    print("DUT responded over MQTT -- it's alive and already on the office WiFi.")
    return True


def _mqtt_first_fallback(run: TestRun, config: BenchConfig) -> None:
    jig = run.jig
    assert jig is not None and run.device_id is not None

    print("Could not join the DUT's SoftAP -- checking whether it's already "
          "connected to the office WiFi before giving up on it.")
    if not config.office_wifi_ssid:
        run.set_step("factory_reset", False,
                     "jig could not join the DUT's SoftAP, and no office WiFi is configured to check instead")
        _mark_remaining_skipped(run, "DUT unreachable via SoftAP, and no office WiFi configured to check via MQTT")
        return

    run.check_stop()
    if not _probe_dut_via_mqtt(run, config):
        run.set_step("factory_reset", False,
                     "jig could not join the DUT's SoftAP, and it did not respond over "
                     "office WiFi/MQTT either -- check it's powered on")
        _mark_remaining_skipped(run, "DUT unreachable via SoftAP or MQTT")
        jig.leave_wifi()
        return

    print("Running the MQTT-phase tests first (DUT found already connected), "
          "then forcing it back into SoftAP for the remaining local tests.")
    run.set_step("wifi_mqtt", True, "found already connected to office WiFi/MQTT -- SoftAP was unreachable")
    run.check_stop()
    _mqtt_phase_tests(run, config)

    run.check_stop()
    print("Rejoining the DUT's SoftAP for the remaining local tests...")
    if jig.join_dut_softap(run.device_id):
        _softap_local_tests(run, config)
    else:
        run.set_step("factory_reset", False,
                     "ship-clean reset sent, but the jig could not rejoin the DUT's SoftAP afterward")
        for name in ("calibrate", "relay_test", "flow_sensor", "rtc"):
            run.set_step(name, False, "skipped -- could not rejoin SoftAP")


def _dut_unix_for_now() -> int:
    """The value to send as rtc_sync's "unix" field so the DUT's RTC ends
    up showing this laptop's own local wall-clock time (India time, per
    2026-09-19 request) -- matching what NTP sync already does for the
    same clock.

    RTCManager::syncFromUnix() hands this straight to RTClib's DateTime(
    unixTime) constructor, which treats the number as a literal UTC epoch
    and derives calendar fields from it -- sending real UTC (time.time())
    would leave the DUT's rtc_date/rtc_time showing UTC, ~5.5h behind
    India time. NTP sync (see firmware's syncNTP()) uses configTime(19800,
    ...) + getLocalTime() to get IST wall-clock fields, then writes them
    directly via syncFromTm() -- no unix conversion at all, so it lands on
    local time by construction. calendar.timegm() here re-encodes our own
    local wall-clock fields the same way: as if they WERE a UTC epoch, so
    DateTime(unixTime) decodes them back to the same local fields NTP
    would produce -- both sync paths now agree on the same convention."""
    return calendar.timegm(datetime.now().timetuple())


def _report_rtc_drift(info: Optional[dict]) -> None:
    """Compares the DUT's currently-reported RTC date/time against this
    laptop's own local clock, and prints a heads-up if it's off by more
    than 2 minutes -- called right before every rtc_sync below overwrites
    it.

    Bench request 2026-09-19: silently overwriting a wildly-wrong RTC
    every run made it easy to never notice a genuinely dead/absent coin
    cell battery (which shows up as a large, real drift every single run
    since the clock keeps resetting to some fixed/drifting point whenever
    power is lost) -- worth a heads-up, not just a silent fix."""
    if not info or not info.get("rtc_date") or not info.get("rtc_time"):
        return
    try:
        dut_dt = datetime.strptime(f"{info['rtc_date']} {info['rtc_time']}", "%d/%m/%Y %H:%M")
    except ValueError:
        return
    drift_s = (datetime.now() - dut_dt).total_seconds()
    if abs(drift_s) > 120:
        sign = "behind" if drift_s > 0 else "ahead of"
        print(f"DUT's RTC is {abs(drift_s) / 60:.1f} min {sign} this laptop's clock "
              f"(DUT reports {info['rtc_date']} {info['rtc_time']}) -- syncing to laptop time now.")


def _softap_local_tests(run: TestRun, config: BenchConfig) -> None:
    """The SoftAP-phase local/physical tests (factory_reset through rtc)
    -- assumes the jig is ALREADY joined to the DUT's SoftAP (caller's
    responsibility). Shared by the normal SoftAP-first flow and the
    MQTT-first fallback (which rejoins SoftAP after forcing the DUT back
    into it)."""
    jig = run.jig
    assert jig is not None

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

    run.check_stop()
    cal = jig.http_command("calibrate", {"ppl": config.expected_calibration_ppl})
    run.set_step("calibrate", cal is not None)

    jig.http_command("relay_test", timeout_s=3)
    relay_on = jig.relay_state_confirmed(expect_on=True)
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
    #
    # 2026-09-19 bench finding: 3 attempts / 1.0s apart wasn't always
    # enough -- a run right after a factory_reset+reboot+rejoin sequence
    # (the MQTT-first fallback's tail) saw manual_on still not land within
    # that budget, silently fell through to pulsing anyway, and reported
    # the false "0.00L" as if it were a real sensor-accuracy failure.
    # startManual() itself is synchronous (cycle_active flips true before
    # the HTTP response even returns -- see Scheduler::startManual()), so
    # a confirmed-false cycle_active really does mean the request never
    # landed, not a timing race to out-wait. More budget, and -- same
    # principle already used for the MQTT-phase version below -- an
    # honest "never landed" failure instead of pulsing blind and mislabeling
    # a delivery failure as a sensor failure.
    before = None
    cycle_confirmed = False
    for _ in range(5):
        jig.http_command("manual_on")
        before = jig.device_info()
        if before and before.get("cycle_active"):
            cycle_confirmed = True
            break
        time.sleep(1.5)
    if not cycle_confirmed:
        run.set_step("flow_sensor", False,
                     "manual_on never confirmed active (cycle_active stayed false after 5 attempts) -- "
                     "could not run the flow test")
    else:
        before_l = (before or {}).get("liters_delivered", 0.0)
        _pulse_visibly(jig, config.flow_test_pulse_count, run=run)
        time.sleep(0.5)
        after = jig.device_info()
        after_l = (after or {}).get("liters_delivered", 0.0)
        jig.http_command("manual_off")
        delivered = after_l - before_l
        expected = config.flow_test_pulse_count / config.expected_calibration_ppl
        within = abs(delivered - expected) <= expected * 0.02
        run.set_step("flow_sensor", within, f"expected {expected:.2f}L, got {delivered:.2f}L")

    run.check_stop()
    _report_rtc_drift(jig.device_info())
    jig.http_command("rtc_sync", {"unix": _dut_unix_for_now()}, timeout_s=3)
    rtc_info = jig.device_info()
    run.set_step("rtc", bool((rtc_info or {}).get("rtc_set")), f"rtc_time={(rtc_info or {}).get('rtc_time', '')}")


def _provision_office_wifi(run: TestRun, config: BenchConfig) -> None:
    """Hands the DUT from the jig's SoftAP over to the office WiFi -- the
    tail end of the old _softap_phase(), split out so the MQTT-first
    fallback (which already knows WiFi/MQTT works, having just used it)
    doesn't redundantly re-provision it. Only called from the normal
    SoftAP-first path, right after _softap_local_tests()."""
    jig = run.jig
    assert jig is not None

    if not config.office_wifi_ssid:
        run.set_step("wifi_mqtt", False, "no office WiFi SSID set -- run with --settings first")
        for name in ("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt"):
            run.set_step(name, False, "skipped -- WiFi/MQTT never connected")
        reset_ack = jig.http_command("factory_reset", timeout_s=5)
        run.set_step("ship_clean_reset", reset_ack is not None, "sent via SoftAP-phase transport")
        run.set_step("factory_reset_confirmed", False, "skipped -- see wifi_mqtt")
        return

    # A single wifi_config request is the same fire-and-forget shape as
    # manual_on/relay_test before those got a retry -- and its return value
    # was being discarded outright: http_command() already tells us
    # synchronously (None) if the request never reached the DUT, but the
    # code went on to leave_wifi() regardless. Bench-confirmed 2026-09-19:
    # this is why wifi_mqtt reported "no WiFi Connected line" even sitting
    # right next to the router -- the DUT's own "[WiFi] Credentials saved"
    # line (see firmware's wifi_config handler) never printed because the
    # request itself never landed, not because the office WiFi was
    # unreachable. Retry like the other single-shot SoftAP commands, and
    # fail this step immediately with a clear reason instead of silently
    # spending the next 32s+20s waiting on a connection that was never
    # going to happen.
    wifi_cfg_ack = None
    for _ in range(3):
        wifi_cfg_ack = jig.http_command("wifi_config", {"ssid": config.office_wifi_ssid, "pass": config.office_wifi_password})
        if wifi_cfg_ack is not None:
            break
        time.sleep(1.0)
    if wifi_cfg_ack is None:
        run.set_step("wifi_mqtt", False, "wifi_config request never reached the DUT over SoftAP -- retried 3x")
        for name in ("relay_test_mqtt", "flow_sensor_mqtt", "rtc_mqtt"):
            run.set_step(name, False, "skipped -- WiFi/MQTT never connected")
        run.set_step("ship_clean_reset", False, "skipped -- broker unreachable")
        run.set_step("factory_reset_confirmed", False, "skipped -- see wifi_mqtt")
        return

    jig.http_command("resume_auto_mode", timeout_s=3)
    jig.leave_wifi()


def _wifi_mqtt_handover(run: TestRun, config: BenchConfig) -> None:
    jig = run.jig
    assert jig is not None and run.device_id is not None
    if run.steps["wifi_mqtt"].passed is False:
        return  # already handled (and all dependent steps set) in _provision_office_wifi --
        # either no SSID configured, or wifi_config itself never reached the DUT

    print(f"Waiting up to {int(WIFI_MQTT_WAIT_S)}s for the DUT to reach the office WiFi + broker...")
    live_log = dut_serial.capture(run.dut_port, WIFI_MQTT_WAIT_S, coordinator=run.dut_tail_coordinator)
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
        # The jig left the DUT's SoftAP at the end of _provision_office_wifi(); now
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

    # Settle delay before the first MQTT command in this phase -- capturing
    # the DUT's serial instead of blindly sleeping through it, so this
    # also picks up the DUT's LAN IP (now printed on the earlier
    # "[WiFi] Reconnected" line itself -- see the DUT firmware -- rather
    # than waiting for the much-later WIFI_STABLE_HOLD_MS=60s SoftAP
    # teardown moment, which a single WiFi blip can push out past any
    # reasonably-sized capture window by resetting the DUT's own
    # stability timer). That IP is what lets relay_test_mqtt/
    # flow_sensor_mqtt/rtc_mqtt verify results via a direct LAN_HTTP_CMD
    # instead of only through the jig's own MQTT status subscription
    # (bench-confirmed 2026-09-18 to be the less reliable of the two
    # under real network conditions). Bench finding, same date: relay_
    # test/manual_on sent immediately after this handover confirms never
    # once arrived at the DUT, while factory_reset sent later in this
    # same function landed every time -- the connection is least stable
    # in the seconds right after WiFi/MQTT first comes up (matches the
    # ASSOC_LEAVE/DNS-fail/rc=-2 churn seen throughout this session), so
    # this still also serves that original settle purpose.
    print("Letting the WiFi/MQTT connection settle before sending commands...")
    settle_log = dut_serial.capture(run.dut_port, 20.0, coordinator=run.dut_tail_coordinator)
    ip_match = re.search(r"IP:\s*(\d{1,3}(?:\.\d{1,3}){3})", live_log + settle_log)
    if ip_match:
        run.dut_lan_ip = ip_match.group(1)
        print(f"DUT's LAN IP: {run.dut_lan_ip} (jig can reach it directly for verification)")
    else:
        print("DUT's LAN IP not seen yet -- relay_test_mqtt/flow_sensor_mqtt/rtc_mqtt "
              "will fall back to MQTT status for verification.")

    run.check_stop()
    _mqtt_phase_tests(run, config)


def _mqtt_phase_tests(run: TestRun, config: BenchConfig) -> None:
    """relay_test_mqtt/flow_sensor_mqtt/rtc_mqtt + the final ship-clean
    reset -- assumes the jig is ALREADY connected to the office WiFi and
    MQTT-bridged to this run's device_id (caller's responsibility).
    Extracted so both the normal WiFi/MQTT-handover path and the
    MQTT-first fallback (which finds the DUT already on MQTT before ever
    touching SoftAP) can run the exact same MQTT-phase logic instead of
    two copies drifting apart."""
    jig = run.jig
    assert jig is not None

    # A single mqtt_command() only confirms the jig published -- not that
    # the DUT received it (QoS0, no session/ack on either PubSubClient).
    # relay_test/manual_on were the only two commands in this phase still
    # using the bare fire-and-forget publish, and were the only two steps
    # that failed consistently across repeated bench runs 2026-09-18 even
    # after the DUT-side WiFi-flap bug was fixed -- ship_clean_reset (which
    # already used mqtt_command_with_retry) passed reliably in the same
    # runs. Switching these two to the same retry-and-confirm helper.
    #
    # Both commands get the same extra retry: a mobile-hotspot bench run
    # (2026-09-18, cleaner network than the office WiFi) showed every
    # other MQTT step pass once the network itself was stable, but
    # relay_test still missed -- then, on a repeat run, relay_test landed
    # fine and manual_on missed instead. Whichever command is first/early
    # in this window is the one exposed, not a fixed one -- so both get
    # the same margin rather than just whichever failed last time.
    # relay_test doesn't need the serial-marker confirmation the other
    # commands rely on -- it has something strictly better: a direct,
    # physical, independent signal (the jig sensing the relay actually
    # close) that the SoftAP-phase relay_test right above in this same
    # file has never once gotten wrong all session. Bench-confirmed
    # 2026-09-18: a run where the relay visibly clicked ON still reported
    # "relay_test never confirmed landing on DUT serial" -- impossible,
    # since handleCommand() only calls relay.testPulse() after already
    # printing "[CMD] relay_test" first, so that line was printed; the
    # marker search just missed it in a corrupted stretch of the capture
    # (the same USB-serial byte-duplication artifact diagnosed earlier
    # this session). Retrying the *send* against the physical sense
    # directly, instead of gating on a serial substring that can go
    # missing even when the command demonstrably landed, removes that
    # whole class of false failure.
    relay_on = None
    for _ in range(4):
        jig.mqtt_command("relay_test")
        relay_on = jig.relay_state_confirmed(expect_on=True, poll_s=5.5)
        if relay_on:
            break
    run.set_step("relay_test_mqtt", relay_on is True,
                 "jig sensed relay ON (via MQTT)" if relay_on else "jig did NOT sense relay closing")

    # 2026-09-19 bench finding: the previous version confirmed manual_on
    # via _publish_confirmed_by_serial() -- watching for "[CMD] manual_on"
    # in the DUT's serial, resending up to 2 more times (3 sends total)
    # if the marker capture window missed it. That's the wrong pattern
    # for THIS specific command: unlike relay_test (each resend just
    # re-triggers a harmless pulse) or factory_reset (idempotent),
    # manual_on's handler (Scheduler::startManual()) resets litersDelivered
    # and the flow pulse counter to zero on every single call. A resend
    # whose delivery is merely delayed (no ack on either PubSubClient,
    # so nothing stops one arriving late) can land *after* this function
    # already confirmed an earlier send, snapshotted "before", and started
    # pulsing -- wiping the count mid-test and producing exactly the false
    # "0.00L" seen on the bench even with cycle_active confirmed true
    # beforehand. Confirming via DUT serial text was also the known
    # USB-serial-corruption-prone path everywhere else this session.
    #
    # Fixed by dropping the serial-marker/resend pattern entirely and
    # using the same pattern already proven for relay_test_mqtt and the
    # SoftAP-phase flow test: send the command, then confirm the ACTUAL
    # resulting device state (cycle_active) directly instead of the
    # command having merely gone out. No further resend is possible once
    # confirmed, since -- being synchronous (see Scheduler::startManual())
    # -- cycle_active reads true immediately once the first send has
    # genuinely landed, so no straggler duplicate is still in flight to
    # land mid-pulse.
    cycle_confirmed = False
    before: dict = {}
    for _ in range(5):
        if run.dut_lan_ip:
            jig.lan_http_command(run.dut_lan_ip, "manual_on")
            before = jig.lan_device_info(run.dut_lan_ip) or {}
        else:
            jig.mqtt_command("manual_on")
            before = jig._poll_status(3.5) or {}
        if before.get("cycle_active"):
            cycle_confirmed = True
            break
        if run.dut_lan_ip:
            time.sleep(1.0)
    if not cycle_confirmed:
        run.set_step("flow_sensor_mqtt", False,
                     "manual_on never confirmed active (cycle_active stayed false) -- could not run the flow test")
    else:
        _pulse_visibly(jig, config.flow_test_pulse_count, run=run)
        # 2026-09-19 bench finding (root-caused with debug_flow_mqtt.py):
        # every single pulse got a real "OK:1" from the jig -- delivery
        # itself was never the problem here. The single follow-up read
        # right after pulsing occasionally came back HTTP_FAIL (a lone LAN
        # HTTP request timing out under load, same "no ack/no guaranteed
        # delivery" class as everything else in this file), and `or
        # before` silently substituted the stale PRE-pulse baseline --
        # manufacturing a false "0.00L" out of a read failure, not a real
        # measurement. Retry the read like every other command here
        # instead of falling back to a fabricated result after only one
        # attempt.
        after = None
        for _ in range(3):
            if run.dut_lan_ip:
                after = jig.lan_device_info(run.dut_lan_ip)
            else:
                after = jig._poll_status(4.0)
            if after is not None:
                break
            time.sleep(1.0)
        verify_via = "via LAN HTTP" if run.dut_lan_ip else "via MQTT status"
        if after is None:
            run.set_step("flow_sensor_mqtt", False,
                         f"could not read the DUT's status after pulsing ({verify_via}, retried 3x) -- "
                         "pulses were sent and acknowledged by the jig, but the result is unconfirmed")
        else:
            before_l, after_l = before.get("liters_delivered", 0.0), after.get("liters_delivered", 0.0)
            delivered = after_l - before_l
            expected = config.flow_test_pulse_count / config.expected_calibration_ppl
            within = abs(delivered - expected) <= expected * 0.02
            run.set_step("flow_sensor_mqtt", within, f"expected {expected:.2f}L, got {delivered:.2f}L ({verify_via})")
        if run.dut_lan_ip:
            jig.lan_http_command(run.dut_lan_ip, "manual_off")
        else:
            jig.mqtt_command_with_retry("manual_off", confirmed_by=lambda s: s.get("cycle_active") is False)

    # 2026-09-19 bench finding: this used to only ever passively READ
    # whatever rtc_set/rtc_time the DUT already happened to be reporting
    # -- no rtc_sync command was ever actually sent in this phase. In the
    # normal SoftAP-first flow that's masked by the SoftAP-phase's own rtc
    # step having already synced it moments earlier, but the MQTT-first
    # fallback runs this before ever touching the local rtc step, so it
    # had nothing to confirm and always came back empty. Actually send
    # the sync here (reporting drift first, same as the SoftAP-phase
    # version) instead of just hoping it's already set.
    if run.dut_lan_ip:
        before_info = jig.lan_device_info(run.dut_lan_ip)
        _report_rtc_drift(before_info)
        # Retried, not a single shot -- 2026-09-19 bench finding: an
        # unconfirmed single LAN HTTP send here did occasionally just drop
        # (came back with rtc_time empty even though the earlier read and
        # the later ship-clean reset's own LAN calls both landed fine
        # around it), same no-guarantee-of-delivery risk every other
        # command in this file already gets a retry for.
        rtc_status = None
        for _ in range(3):
            jig.lan_http_command(run.dut_lan_ip, "rtc_sync", {"unix": _dut_unix_for_now()})
            rtc_status = jig.lan_device_info(run.dut_lan_ip)
            if rtc_status and rtc_status.get("rtc_set"):
                break
            time.sleep(1.0)
        verify_via = "via LAN HTTP"
    else:
        _report_rtc_drift(jig.mqtt_status())
        # Retried and confirmed the same way ship_clean_reset's
        # force_local_mode is -- a bare mqtt_command() here has the same
        # no-ack delivery risk as every other MQTT-phase command in this
        # file.
        rtc_status = jig.mqtt_command_with_retry(
            "rtc_sync", {"unix": _dut_unix_for_now()},
            attempts=4, interval_s=3.0,
            confirmed_by=lambda s: s.get("rtc_set") is True,
        )
        verify_via = "via MQTT status"
    run.set_step("rtc_mqtt", bool((rtc_status or {}).get("rtc_set")),
                 f"rtc_time={(rtc_status or {}).get('rtc_time', '')} ({verify_via})")

    run.check_stop()
    _ship_clean_reset(run, jig)


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
        captured["log"] = dut_serial.capture(
            run.dut_port, RESET_CONFIRM_WAIT_S, coordinator=run.dut_tail_coordinator,
        )

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
            extra = dut_serial.capture(run.dut_port, 5.0, coordinator=run.dut_tail_coordinator)
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
