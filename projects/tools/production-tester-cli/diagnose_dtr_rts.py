#!/usr/bin/env python3
"""
Standalone diagnostic for the DUT-resets-when-a-serial-monitor-connects
bench finding (2026-09-19): tries opening the DUT's port with every
DTR/RTS combination, one at a time, and reports whether the board's boot
banner ("ets Jul 29 2019...") appears right after -- which means THAT
combination reset the board just by opening the port. Real hardware
data instead of guessing which polarity this specific board's
auto-reset circuit uses.

Run this directly (no jig needed, just the DUT plugged in):
    python3 diagnose_dtr_rts.py --port COM10
    python3 diagnose_dtr_rts.py --port /dev/cu.usbserial-2

Watch the DUT while it runs -- if it has a WiFi/status LED, you can
also just watch for it blinking/going dark as a physical double-check
of what the tool reports. Send the full printed output back.
"""
from __future__ import annotations

import argparse
import time

import serial

BOOT_MARKER = "ets Jul 29 2019"

# None = "don't touch it, let pyserial/the OS use its own default" --
# included as a baseline, since that's what the tool has always done.
COMBINATIONS = [
    ("leave alone (previous/default behavior)", None, None),
    ("DTR=False, RTS=False", False, False),
    ("DTR=True,  RTS=True",  True,  True),
    ("DTR=True,  RTS=False", True,  False),
    ("DTR=False, RTS=True",  False, True),
]


def try_combo(port: str, baud: int, dtr, rts, settle_s: float = 3.0) -> str:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.3
    if dtr is not None:
        ser.dtr = dtr
    if rts is not None:
        ser.rts = rts
    try:
        ser.open()
    except serial.SerialException as e:
        return f"COULD NOT OPEN: {e}"

    collected = ""
    deadline = time.monotonic() + settle_s
    while time.monotonic() < deadline:
        chunk = ser.read(512)
        if chunk:
            collected += chunk.decode("utf-8", errors="replace")
    ser.close()

    if BOOT_MARKER in collected:
        return "RESET -- boot banner seen right after opening with this setting"
    if collected.strip():
        return f"no reset -- board was already quiet/running, saw: {collected.strip()[:200]!r}"
    return "no reset -- silence (board may already be idle/quiet, or nothing to print right now)"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True, help="DUT's serial port, e.g. COM10 or /dev/cu.usbserial-2")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--settle-s", type=float, default=3.0, help="Seconds to watch after each open (default 3)")
    args = parser.parse_args()

    print(f"Testing DTR/RTS combinations on {args.port} -- watch for resets.\n")
    for label, dtr, rts in COMBINATIONS:
        print(f"---- {label} ----")
        result = try_combo(args.port, args.baud, dtr, rts, settle_s=args.settle_s)
        print(result)
        print()
        time.sleep(2.0)  # let things settle before the next attempt regardless of outcome

    print("Done. Send this whole output back -- whichever combination(s)")
    print("say 'no reset' are the ones safe to use for the real tool.")


if __name__ == "__main__":
    main()
