"""
Finds which of the connected USB-serial ports is the jig and which is
the DUT -- mirrors the Android app's probeAllAndAssignRoles(): PING
every candidate port, whichever replies PONG is the jig, and (assuming
exactly one other CP210x/CH34x-style port is present) that's the DUT.
No fixed port assumption, no per-unit configuration.
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable, Optional

import serial.tools.list_ports

from .jig_client import JigClient, probe_for_jig


@dataclass
class HardwareResult:
    jig: Optional[JigClient]
    jig_port: Optional[str]
    dut_port: Optional[str]
    all_ports: list[str]


def _find_hardware_once(on_jig_line: Optional[Callable[[str], None]] = None) -> HardwareResult:
    # vid is None for OS pseudo-ports (macOS's /dev/cu.debug-console,
    # /dev/cu.Bluetooth-Incoming-Port, etc.) -- confirmed on the bench
    # 2026-09-18: without this filter, one of those got picked as the
    # "remaining" port and treated as the DUT, since it's still enumerated
    # by list_ports.comports() alongside the two real CP2102 devices.
    # Every actual USB-serial adapter reports a real vid/pid.
    ports = [p.device for p in serial.tools.list_ports.comports() if p.vid is not None]
    jig = None
    jig_port = None
    remaining = list(ports)
    for port in ports:
        # on_jig_line is passed to every candidate here, including ones
        # that turn out to be the DUT -- probing sends a single harmless
        # PING before identity is known, so at worst a stray "PING"/"(no
        # reply)" pair shows up once in a live jig console at startup.
        candidate = probe_for_jig(port, on_line=on_jig_line)
        if candidate is not None:
            jig = candidate
            jig_port = port
            remaining.remove(port)
            break
    dut_port = remaining[0] if remaining else None
    return HardwareResult(jig=jig, jig_port=jig_port, dut_port=dut_port, all_ports=ports)


def find_hardware(
    attempts: int = 3, retry_delay_s: float = 1.5,
    on_jig_line: Optional[Callable[[str], None]] = None,
) -> HardwareResult:
    """find_hardware(), retried a few times before giving up. Bench-
    confirmed 2026-09-18: back-to-back bench runs (each ending in an
    esptool hard reset via RTS pin) occasionally hit a genuinely empty
    port list on the very next scan -- macOS hadn't finished
    re-enumerating the USB-serial devices yet. A short retry clears it;
    a single scan doesn't."""
    result = _find_hardware_once(on_jig_line)
    for _ in range(attempts - 1):
        if result.jig is not None and result.dut_port is not None:
            return result
        time.sleep(retry_delay_s)
        result = _find_hardware_once(on_jig_line)
    return result
