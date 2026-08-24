"""Tails the DUT's serial boot log and checks it against expected markers.

Real, runnable today. Requires: pip install pyserial
"""
import re
import time

import serial

# Markers that MUST appear, in any order, within the boot window.
REQUIRED_MARKERS = [
    r"\[BOOT\] SmartWaterController starting",
    r"\[NVS\] Initialized OK",
    r"\[RTC\] Time:",
    r"\[RELAY\] Initialized on pin",
    r"\[FLOW\] Initialized on pin",
    r"\[BOOT\] Device ID:",
]

# [E]-tagged lines that are EXPECTED on a freshly-flashed or factory-reset
# unit (no MQTT config saved yet) -- not failures. Any [E] line not
# matching one of these is treated as a real defect.
BENIGN_ERROR_PATTERNS = [
    r"nvs_get_str len fail: mqtt_broker NOT_FOUND",
    r"nvs_get_str len fail: mqtt_user NOT_FOUND",
    r"nvs_get_str len fail: mqtt_pass NOT_FOUND",
    r"Bus already started in Master Mode",  # I2C re-init warning, harmless
]

DEVICE_ID_PATTERN = re.compile(r"\[BOOT\] Device ID: (\S+)")


def capture_boot_log(port: str, baud: int = 115200, window_s: float = 10.0) -> dict:
    """Opens the serial port, resets the DUT (DTR/RTS toggle, same as
    PlatformIO's monitor does), and captures `window_s` seconds of boot
    output. Returns a dict with the raw log, device_id (if found), which
    required markers were seen, and any unexpected error lines.
    """
    lines: list[str] = []
    with serial.Serial(port, baud, timeout=0.5) as ser:
        # Toggle DTR/RTS to reset the ESP32 into a fresh boot, exactly
        # like `pio device monitor` does on open.
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False

        deadline = time.time() + window_s
        while time.time() < deadline:
            raw = ser.readline()
            if raw:
                try:
                    line = raw.decode("utf-8", errors="replace").rstrip()
                except Exception:
                    continue
                if line:
                    lines.append(line)

    full_log = "\n".join(lines)

    device_id = None
    m = DEVICE_ID_PATTERN.search(full_log)
    if m:
        device_id = m.group(1)

    markers_seen = {
        marker: bool(re.search(marker, full_log)) for marker in REQUIRED_MARKERS
    }

    unexpected_errors = []
    for line in lines:
        if "[E]" in line:
            if not any(re.search(pat, line) for pat in BENIGN_ERROR_PATTERNS):
                unexpected_errors.append(line)

    all_markers_ok = all(markers_seen.values())
    passed = all_markers_ok and not unexpected_errors

    return {
        "passed": passed,
        "device_id": device_id,
        "markers_seen": markers_seen,
        "unexpected_errors": unexpected_errors,
        "raw_log": full_log,
    }


if __name__ == "__main__":
    import sys

    port_arg = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-0001"
    result = capture_boot_log(port_arg)
    print(result["raw_log"])
    print("---")
    print("Device ID:", result["device_id"])
    for marker, seen in result["markers_seen"].items():
        print(f"  {'OK' if seen else 'MISSING'}: {marker}")
    if result["unexpected_errors"]:
        print("Unexpected errors:")
        for e in result["unexpected_errors"]:
            print("  ", e)
    print("BOOT LOG:", "PASS" if result["passed"] else "FAIL")
