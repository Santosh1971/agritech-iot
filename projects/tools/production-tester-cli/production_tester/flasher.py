"""
Flashes the DUT via Espressif's own esptool -- the official, battle-tested
tool every ESP32 developer already uses, invoked as a subprocess. This
replaces the Android app's whole esp-serial-flasher JNI/C reimplementation
of the SLIP flashing protocol: that existed only because Android couldn't
just shell out to a real Python tool. A laptop can, so nothing needed
reinventing here.

Standard ESP32/PlatformIO flash layout -- matches
products/FG1-flowguard/firmware's min_spiffs.csv partition table, same
offsets the Android app used.
"""
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

BOOTLOADER_OFFSET = "0x1000"
PARTITIONS_OFFSET = "0x8000"
APP_OFFSET = "0x10000"

MAC_PATTERN = re.compile(r"([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})")


@dataclass
class FlashResult:
    ok: bool
    detail: str


def _run(port: str, baud: int, args: list[str], timeout_s: float = 120) -> subprocess.CompletedProcess:
    cmd = ["esptool", "--chip", "esp32", "--port", port, "--baud", str(baud), *args]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)


def _run_with_retry(
    port: str, baud: int, args: list[str], timeout_s: float, attempts: int = 3,
) -> FlashResult:
    """esptool occasionally hits a transient serial hiccup mid-operation
    ("Invalid head of packet", "No more data to read from the serial
    port") that a plain retry clears immediately -- confirmed twice on
    the bench 2026-09-18, both times the exact same command succeeded on
    the very next try with nothing else changed. Not a real flashing
    bug, just USB/serial noise esptool doesn't retry internally for --
    so retry here instead of failing the whole unit over it."""
    last = FlashResult(False, "never attempted")
    for attempt in range(1, attempts + 1):
        try:
            result = _run(port, baud, args, timeout_s=timeout_s)
        except (subprocess.TimeoutExpired, OSError) as e:
            last = FlashResult(False, str(e))
            continue
        if result.returncode == 0:
            return FlashResult(True, "")
        last = FlashResult(False, result.stderr.strip() or result.stdout.strip())
    return FlashResult(False, f"{last.detail} (failed after {attempts} attempts)")


def read_mac(port: str, baud: int = 115200) -> Optional[str]:
    """Reads the connected chip's burned-in MAC. Works on a blank chip too
    -- it's an eFuse value, not something firmware has to report. Returns
    hex with no separators (e.g. "3076F593B468"), matching what the
    backend's device-allowlist check expects."""
    try:
        result = _run(port, baud, ["read-mac"], timeout_s=20)
    except (subprocess.TimeoutExpired, OSError):
        return None
    match = MAC_PATTERN.search(result.stdout)
    if not match:
        return None
    return match.group(1).replace(":", "").upper()


def erase_chip(port: str, baud: int = 115200) -> FlashResult:
    """Full chip erase before every flash -- not just the three segments
    write_firmware() writes. 2026-09-16 finding on the Android app (same
    reasoning applies here): reflashing a unit for a new round left its
    old WiFi/MQTT credentials in place, because NVS lives outside the
    bootloader/partitions/app offsets and a partial flash never touches
    it. A blank-chip guarantee matters more than the extra time this
    costs, since every unit's NVS otherwise carries whatever the previous
    test round wrote."""
    return _run_with_retry(port, baud, ["erase-flash"], timeout_s=60)


def write_firmware(
    port: str, bootloader: Path, partitions: Path, app: Path, baud: int = 921600,
) -> FlashResult:
    args = [
        "write-flash",
        BOOTLOADER_OFFSET, str(bootloader),
        PARTITIONS_OFFSET, str(partitions),
        APP_OFFSET, str(app),
    ]
    return _run_with_retry(port, baud, args, timeout_s=120)
