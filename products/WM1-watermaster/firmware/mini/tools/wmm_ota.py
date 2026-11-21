#!/usr/bin/env python3
"""Update a WM1-Mini / WM-Mini C3 board's firmware over WiFi (push, firmware 0.4.0+).
The PC must be on the same WiFi as the board.

    python3 wmm_ota.py 192.168.1.14                  # default: the wm_mini_c3 build
    python3 wmm_ota.py 192.168.1.14 path/to/firmware.bin

Password: WMM_OTA_PASS (env) or ~/.config/agrisense/wmm_ota_pass, the value
compiled into the firmware by wmm_secrets.py. The board writes the image to its
spare slot and switches only after the whole image arrived and its MD5 matched;
then it restarts and this tool waits for it to come back.
"""
import base64
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, "..", "WM1Firmware", ".pio", "build", "wm_mini_c3", "firmware.bin")


def password():
    pw = os.environ.get("WMM_OTA_PASS", "").strip()
    f = os.path.expanduser("~/.config/agrisense/wmm_ota_pass")
    if not pw and os.path.exists(f):
        pw = open(f).read().strip()
    if not pw:
        sys.exit("no OTA password: set WMM_OTA_PASS or ~/.config/agrisense/wmm_ota_pass")
    return pw


def state(host):
    with urllib.request.urlopen(f"http://{host}/api/state", timeout=4) as r:
        return json.loads(r.read())


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    host = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_BIN
    data = open(path, "rb").read()
    before = state(host)
    print(f"{before.get('device_id')} ({before.get('board')}) at {host}: fw {before.get('fw')} -> uploading {len(data)} bytes")
    req = urllib.request.Request(f"http://{host}/api/ota", data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("X-MD5", hashlib.md5(data).hexdigest())
    req.add_header("Authorization", "Basic " + base64.b64encode(f"ota:{password()}".encode()).decode())
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            print("board:", r.read().decode(), f"({time.time() - t0:.0f} s)")
    except urllib.error.HTTPError as e:
        sys.exit(f"update refused: HTTP {e.code} {e.read().decode(errors='replace')}")
    except (TimeoutError, OSError, urllib.error.URLError) as e:
        print(f"no reply from the board ({type(e).__name__}); checking whether it restarted...")
    print("waiting for the board to restart...")
    time.sleep(6)
    for _ in range(30):
        try:
            s = state(host)
            print(f"{s.get('device_id')} is back: fw {s.get('fw')}, uptime {s.get('uptime_s')} s")
            if s.get("uptime_s", 999) > 120:
                sys.exit("the board did not restart: the update was not applied")
            return
        except Exception:
            time.sleep(2)
    sys.exit("board did not come back within 60 s: check power and WiFi; if the new firmware cannot start, re-flash by USB")


if __name__ == "__main__":
    main()
