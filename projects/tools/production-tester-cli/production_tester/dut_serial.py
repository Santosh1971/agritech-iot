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
        time.sleep(0.15)  # give tail()'s loop a moment to actually close its handle
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
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
                ser = serial.Serial(port, baud, timeout=0.3)
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
