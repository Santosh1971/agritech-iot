#!/usr/bin/env python3
"""Update a WM2 node's firmware over WiFi (firmware 0.2.7 or later).

    python3 wm2_ota.py 192.168.1.14                 # default: .pio/build/wm2/firmware.bin
    python3 wm2_ota.py 192.168.1.14 path/to/firmware.bin

The password is WM2_OTA_PASS (env) or ~/.config/agrisense/wm2_ota_pass, the
same value that was compiled into the firmware. The node writes the image to
its spare slot and only switches over once the whole image arrived and its
MD5 matched (an interrupted or corrupt upload leaves the running firmware in
place); then it restarts and this tool waits for the new version.
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
DEFAULT_BIN = os.path.join(HERE, "..", "wm2", ".pio", "build", "wm2", "firmware.bin")


def password():
    pw = os.environ.get("WM2_OTA_PASS", "").strip()
    f = os.path.expanduser("~/.config/agrisense/wm2_ota_pass")
    if not pw and os.path.exists(f):
        pw = open(f).read().strip()
    if not pw:
        sys.exit("no OTA password: set WM2_OTA_PASS or ~/.config/agrisense/wm2_ota_pass")
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
    print(f"{before.get('dev')} at {host}: fw {before.get('fw')} -> uploading {len(data)} bytes from {path}")
    req = urllib.request.Request(f"http://{host}/api/ota", data=data, method="POST")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("X-MD5", hashlib.md5(data).hexdigest())
    req.add_header("Authorization", "Basic " + base64.b64encode(f"wm2:{password()}".encode()).decode())
    t0 = time.time()
    try:
        with urllib.request.urlopen(req, timeout=180) as r:
            print("node:", r.read().decode(), f"({time.time() - t0:.0f} s)")
    except urllib.error.HTTPError as e:
        sys.exit(f"update refused: HTTP {e.code} {e.read().decode(errors='replace')}")
    except (TimeoutError, OSError, urllib.error.URLError) as e:
        # The node can restart before its reply reaches us: judge by the version it comes back with.
        print(f"no reply from the node ({type(e).__name__}); checking whether it restarted...")
    print("waiting for the node to restart...")
    time.sleep(6)
    for _ in range(30):
        try:
            s = state(host)
            print(f"{s.get('dev')} is back: fw {s.get('fw')}, reset reason {s.get('reset')}, uptime {s.get('uptime_s')} s")
            if s.get("uptime_s", 999) > 120:
                sys.exit("the node did not restart: the update was not applied")
            return
        except Exception:
            time.sleep(2)
    sys.exit("node did not come back within 60 s: check it (power, WiFi); if the new firmware cannot start, re-flash by USB")


if __name__ == "__main__":
    main()
