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

import time

import serial


def capture(port: str, duration_s: float, baud: int = 115200) -> str:
    """Opens [port], reads whatever the DUT prints for duration_s, and
    returns the printable text (filtering out non-printable bytes the
    same way the Android app's capture did). Returns "" on any failure to
    open the port -- a diagnostic add-on, not something that should fail
    the actual test step if the capture itself doesn't work."""
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
