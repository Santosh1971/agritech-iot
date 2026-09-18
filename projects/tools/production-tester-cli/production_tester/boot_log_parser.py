"""
Checks a captured boot log against the same required markers and
benign-error allowlist as the Android app's BootLogParser.kt (which in
turn credits testing/serial_monitor.py). Kept in sync by hand -- update
all three if the firmware's boot log format ever changes.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Optional

REQUIRED_MARKERS = [
    (re.compile(r"\[BOOT\] SmartWaterController starting"), "Boot"),
    (re.compile(r"\[NVS\] Initialized OK"), "NVS"),
    (re.compile(r"\[RTC\] Time:"), "RTC"),
    (re.compile(r"\[RELAY\] Initialized on pin"), "Relay"),
    (re.compile(r"\[FLOW\] Initialized on pin"), "Flow"),
    (re.compile(r"\[BOOT\] Device ID:"), "Device ID"),
]

# Only ever appear on a genuinely blank NVS partition (a real chip erase,
# not just an app-partition reflash) -- every actual production unit
# starts blank, so these are the common case, not an edge case.
BENIGN_ERROR_PATTERNS = [re.compile(p) for p in [
    r"nvs_get_str len fail: mqtt_broker NOT_FOUND",
    r"nvs_get_str len fail: mqtt_user NOT_FOUND",
    r"nvs_get_str len fail: mqtt_pass NOT_FOUND",
    r"nvs_get_str len fail: wifi_ssid NOT_FOUND",
    r"nvs_get_str len fail: wifi_pass NOT_FOUND",
    r"nvs_get_blob len fail: rs_liters NOT_FOUND",
    r"nvs_get_str len fail: rs_by NOT_FOUND",
    r"Bus already started in Master Mode",
]]

DEVICE_ID_PATTERN = re.compile(r"\[BOOT\] Device ID: (\S+)")
FIRMWARE_VERSION_PATTERN = re.compile(r"\[BOOT\] Firmware version: (\S+)")


@dataclass
class BootLogResult:
    passed: bool
    device_id: Optional[str]
    firmware_version: Optional[str]
    markers_seen: dict
    unexpected_errors: list


def parse(raw_log: str) -> BootLogResult:
    markers_seen = {label: bool(regex.search(raw_log)) for regex, label in REQUIRED_MARKERS}
    device_id_match = DEVICE_ID_PATTERN.search(raw_log)
    device_id = device_id_match.group(1) if device_id_match else None
    fw_match = FIRMWARE_VERSION_PATTERN.search(raw_log)
    firmware_version = fw_match.group(1) if fw_match else None

    unexpected_errors = [
        line for line in raw_log.splitlines()
        if "[E]" in line and not any(p.search(line) for p in BENIGN_ERROR_PATTERNS)
    ]

    passed = all(markers_seen.values()) and not unexpected_errors
    return BootLogResult(passed, device_id, firmware_version, markers_seen, unexpected_errors)
