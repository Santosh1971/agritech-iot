"""Wraps PlatformIO to flash the FG1 firmware onto a connected DUT.

Real, runnable today -- doesn't depend on the test jig existing, only
on a DUT plugged in via USB.
"""
import subprocess
import sys
from pathlib import Path

FIRMWARE_DIR = Path(__file__).resolve().parent.parent / "firmware"


def flash(env: str = "esp32dev", port: str | None = None, timeout_s: int = 120) -> tuple[bool, str]:
    """Run `pio run -e <env> -t upload`. Returns (success, combined_output).

    env: PlatformIO environment name -- "esp32dev" (DS3231, field build)
         or "esp32dev_ds1307" (bench DS1307 build). See platformio.ini.
    port: explicit upload port (e.g. "/dev/cu.usbserial-0001" or "COM6").
          If None, uses whatever's configured in platformio.ini.
    """
    cmd = ["pio", "run", "-e", env, "-t", "upload"]
    if port:
        cmd += ["--upload-port", port]

    try:
        result = subprocess.run(
            cmd, cwd=FIRMWARE_DIR, capture_output=True, text=True, timeout=timeout_s,
        )
    except subprocess.TimeoutExpired as e:
        return False, f"Flash timed out after {timeout_s}s\n{e}"
    except FileNotFoundError:
        return False, "`pio` not found on PATH -- install PlatformIO CLI first."

    output = result.stdout + result.stderr
    success = result.returncode == 0 and "SUCCESS" in output.upper()
    return success, output


if __name__ == "__main__":
    env_arg = sys.argv[1] if len(sys.argv) > 1 else "esp32dev"
    ok, log = flash(env_arg)
    print(log)
    print("FLASH:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
