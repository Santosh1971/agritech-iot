"""
Passive serial listening on the DUT's own USB link -- boot log capture
right after flashing, and live capture during the WiFi/MQTT wait and the
final reset-confirmation (mirrors the Android app's UsbSerialTransport.
captureBootLog(), minus that whole investigation: pyserial's blocking
read over a real OS serial port on a laptop doesn't have the Android
USB-serial library's documented small-buffer/high-baud data-loss
behavior that caused most of that debugging.
"""
from __future__ import annotations

import threading
import time
from typing import Optional

import serial

# tail()'s own read() call below blocks for up to this long before it even
# gets back around to checking coordinator.is_paused -- every settle sleep
# after coordinator.pause() (here and in cli.py's _flash_and_boot()) must
# be strictly longer than this, or the pause can still be racing an
# in-flight read when the caller tries to open the port itself. 2026-09-19
# bench finding: a 0.15s settle (under this value) worked on macOS but
# reliably failed ESP32 MAC reads on Windows -- Windows holds a COM port
# far more exclusively than macOS does, so the same race that macOS
# happened to tolerate hard-failed there instead.
TAIL_READ_TIMEOUT_S = 0.3
TAIL_SETTLE_S = TAIL_READ_TIMEOUT_S + 0.2


def _open_passive(port: str, baud: int, timeout: float) -> serial.Serial:
    """Opens [port] for passive/read-only listening without asserting
    DTR/RTS. Constructing with no port yet and setting dtr/rts False
    BEFORE calling open() (rather than opening with pyserial's one-shot
    Serial(port, baud, ...) and changing them after) avoids the transient
    pulse on open that many USB-serial drivers wire straight into the
    ESP32's own EN/GPIO0 auto-reset circuit -- the same one esptool
    deliberately drives, on purpose, to reset the chip for flashing.

    2026-09-19 bench finding: a Windows (CP210x) bench was seeing
    genuine POWERON_RESET reboots on the DUT, clustered specifically
    around repeated serial reconnects from tail()/capture() below --
    while the same physical board, at the same time, could be commanded
    fine over WiFi via the phone app, which never opens a USB-serial
    connection to it at all. That ruled out a power/cable problem and
    pointed squarely at this open-triggers-reset behavior instead --
    invisible all session on macOS, whose driver doesn't assert these
    lines the same way (or care as much when it does)."""
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


class TailCoordinator:
    """Coordinates tail() (a best-effort live-view background reader)
    with capture()'s exclusive, timed reads elsewhere in this module --
    both open their own serial.Serial() handle to the same DUT port.

    Bench-confirmed 2026-09-18: this matters more than a plain "port
    busy" exception at open time. Two separate handles briefly open to
    the same physical port at once (tail() already open when capture()
    also opens successfully) can corrupt a read on macOS enough to raise
    an unrelated-looking OSError ("device reports readiness to read but
    returned no data") that isn't a serial.SerialException and so wasn't
    being caught anywhere -- it crashed a whole run. Every capture() call
    now pauses the tailer (which closes its own handle and backs off)
    before opening, and resumes it after closing -- one owner of the
    port at a time, not two racing."""

    def __init__(self) -> None:
        self._paused = threading.Event()

    def pause(self) -> None:
        self._paused.set()

    def resume(self) -> None:
        self._paused.clear()

    @property
    def is_paused(self) -> bool:
        return self._paused.is_set()


def capture(
    port: str, duration_s: float, baud: int = 115200,
    coordinator: Optional[TailCoordinator] = None,
) -> str:
    """Opens [port], reads whatever the DUT prints for duration_s, and
    returns the printable text (filtering out non-printable bytes the
    same way the Android app's capture did). Returns "" on any failure to
    open the port -- a diagnostic add-on, not something that should fail
    the actual test step if the capture itself doesn't work."""
    if coordinator is not None:
        coordinator.pause()
        time.sleep(TAIL_SETTLE_S)  # give tail()'s loop a moment to actually close its handle
    try:
        ser = _open_passive(port, baud, 0.5)
    except serial.SerialException:
        return ""
    try:
        out = bytearray()
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            chunk = ser.read(512)
            if chunk:
                out.extend(chunk)
        text = out.decode("utf-8", errors="replace")
        return "".join(c for c in text if c in "\n\r\t" or (0x20 <= ord(c) <= 0x7E))
    finally:
        ser.close()
        if coordinator is not None:
            coordinator.resume()


def tail(
    port: str, on_line, stop_event, coordinator: Optional[TailCoordinator] = None,
    baud: int = 115200, retry_delay_s: float = 0.5,
) -> None:
    """Continuously reads [port] and calls on_line(text) once per line,
    for as long as [stop_event] isn't set -- for a UI to show a live feed
    alongside the guided test, not something any test step depends on.
    Closes its own handle and backs off entirely while [coordinator] is
    paused (see TailCoordinator), instead of trying to hold the port open
    alongside a concurrent exclusive capture() call."""
    ser: Optional[serial.Serial] = None
    buf = bytearray()
    while not stop_event.is_set():
        if coordinator is not None and coordinator.is_paused:
            if ser is not None:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
            time.sleep(0.1)
            continue
        if ser is None:
            try:
                ser = _open_passive(port, baud, TAIL_READ_TIMEOUT_S)
            except serial.SerialException:
                time.sleep(retry_delay_s)
                continue
        try:
            chunk = ser.read(512)
        except serial.SerialException:
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(retry_delay_s)
            continue
        if not chunk:
            continue
        buf.extend(chunk)
        while b"\n" in buf:
            line, _, rest = buf.partition(b"\n")
            buf = bytearray(rest)
            text = line.decode("utf-8", errors="replace").replace("\r", "")
            text = "".join(c for c in text if c in "\t" or (0x20 <= ord(c) <= 0x7E))
            if text:
                on_line(text)
    if ser is not None:
        try:
            ser.close()
        except Exception:
            pass


def collapse_repeats(text: str) -> str:
    """Collapses runs of 3+ consecutive identical lines into one line plus
    a "(repeated Nx)" note -- purely a display aid, same as the Android
    app's collapseRepeats(); callers should still run marker/substring
    checks against the raw text, not this collapsed version."""
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        j = i + 1
        while j < len(lines) and lines[j] == lines[i]:
            j += 1
        count = j - i
        out.append(lines[i] + (f"  (repeated {count}x)" if count >= 3 else ""))
        i = j
    return "\n".join(out)
