"""FG1 Flash Bridge — a thin HTTP face on the existing flasher.py /
serial_monitor.py / results_logger.py, so the phone app (or a browser)
can trigger a flash and read results over WiFi instead of a terminal.

See docs/testing/PRODUCTION_TOOL_SPEC.md section 7 for the design.
This is intentionally a shim, not a rewrite -- all the actual flashing/
boot-log/logging logic still lives in the existing modules.

Run on the bench laptop (same machine the DUT is plugged into via USB):
    pip install flask
    python flash_bridge.py
    # -> Flash Bridge listening on http://0.0.0.0:8787
    #    Open http://<this-laptop's-LAN-IP>:8787/ in a browser to test
    #    it standalone before the phone app is ready.

Bench-network only (see PRODUCTION_TOOL_SPEC.md section 7 "Auth: none")
-- do not run this on a network that also carries customer traffic.
"""
import json
import shutil
import socket
import threading
import time
from pathlib import Path

import serial
from flask import Flask, Response, jsonify, request

import flasher
import results_logger
import serial_monitor

app = Flask(__name__)

# Set this to the jig's own USB-serial port if it's connected to this
# same laptop (recommended -- see PRODUCTION_TOOL_SPEC.md section 6.4).
# Lets this bridge tell the jig exactly which SSID to target the moment
# a boot log confirms a device ID, instead of the jig guessing by WiFi
# signal strength alone -- the fix for ambiguity when multiple units
# are powered near the bench at once. Leave as None if the jig only has
# WiFi (no USB link to this laptop); it falls back to auto-scan.
JIG_SERIAL_PORT: str | None = "/dev/cu.usbserial-110"

_jig_serial: serial.Serial | None = None
_jig_serial_lock = threading.Lock()


def _get_jig_serial() -> serial.Serial | None:
    """Lazily opens, and keeps open, a single persistent connection to
    the jig. Deliberately not reopened per-command: opening a fresh
    pyserial connection can pulse DTR/RTS and reset boards with an
    auto-reset circuit (the jig has one), which would drop its current
    WiFi join mid-test.
    """
    global _jig_serial
    if not JIG_SERIAL_PORT:
        return None
    if _jig_serial is None or not _jig_serial.is_open:
        try:
            _jig_serial = serial.Serial(JIG_SERIAL_PORT, 115200, timeout=3.0)
            time.sleep(2)  # in case this open *did* reset it, let it finish booting
            _jig_serial.reset_input_buffer()
            print(f"[jig] Connected on {JIG_SERIAL_PORT}")
        except Exception as e:
            print(f"[jig] Could not open {JIG_SERIAL_PORT}: {e}")
            _jig_serial = None
    return _jig_serial


def notify_jig_target(device_id: str) -> bool:
    """Tells the jig to pin onto this exact SSID -- see the jig
    firmware's header comment ("Targeting") for the full picture.
    """
    ser = _get_jig_serial()
    if ser is None:
        return False
    with _jig_serial_lock:
        try:
            ser.reset_input_buffer()
            ser.write(f"TARGET:{device_id}\n".encode("ascii"))
            resp = ser.readline().decode("ascii", errors="replace").strip()
            print(f"[jig] TARGET:{device_id} -> {resp or '(no response)'}")
            return resp.startswith("OK")
        except Exception as e:
            print(f"[jig] notify_jig_target error: {e}")
            return False


def get_lan_ip() -> str | None:
    """Best-effort LAN IP for this machine -- shown on the dashboard so
    the operator can read it straight off the laptop screen instead of
    running ifconfig, and re-enter it in the phone app whenever the
    laptop's DHCP lease changes (see PRODUCTION_TOOL_SPEC.md section 7).
    Doesn't actually send anything -- connect() on a UDP socket just
    picks the outbound interface/address the OS would use.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return None
    finally:
        s.close()

DEFAULT_ENV = "esp32dev_ds1307"
BRIDGE_PORT = 8787


def _cors(resp: Response) -> Response:
    resp.headers["Access-Control-Allow-Origin"] = "*"
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type"
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    return resp


@app.after_request
def add_cors(resp):
    return _cors(resp)


@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "ok": True,
        "pio_found": shutil.which("pio") is not None,
        "firmware_dir": str(flasher.FIRMWARE_DIR),
        "lan_ip": get_lan_ip(),
        "port": BRIDGE_PORT,
    })


@app.route("/flash", methods=["GET", "POST"])
def flash():
    """Streams newline-delimited JSON, one object per line, so a client
    can show live progress instead of a blind spinner:
        {"type": "log", "line": "..."}       -- one per PlatformIO output line
        {"type": "result", "passed": true}   -- always the final line
    """
    env = request.values.get("env", DEFAULT_ENV)
    port = request.values.get("port") or None
    timeout_s = int(request.values.get("timeout_s", 120))

    def generate():
        # Also echo to this process's own stdout -- lets whoever's at the
        # laptop follow along (or debug after the fact) without needing
        # the phone screen, since the phone only sees what streamed to
        # it live.
        for line in flasher.flash_stream(env=env, port=port, timeout_s=timeout_s):
            if line == "FLASH:PASS":
                print("[flash] RESULT: PASS")
                yield json.dumps({"type": "result", "passed": True}) + "\n"
            elif line == "FLASH:FAIL":
                print("[flash] RESULT: FAIL")
                yield json.dumps({"type": "result", "passed": False}) + "\n"
            else:
                print(f"[flash] {line}")
                yield json.dumps({"type": "log", "line": line}) + "\n"

    return Response(generate(), mimetype="application/x-ndjson")


@app.route("/build", methods=["GET", "POST"])
def build():
    """Same NDJSON streaming shape as /flash, but compiles only (no
    upload, no DUT/port needed) -- for tools/flasher-tester-app, which
    flashes the DUT itself natively over USB-OTG and just needs this
    laptop to produce the .bin segments first.
    """
    env = request.values.get("env", DEFAULT_ENV)
    timeout_s = int(request.values.get("timeout_s", 180))

    def generate():
        for line in flasher.build_stream(env=env, timeout_s=timeout_s):
            if line == "FLASH:PASS":
                print("[build] RESULT: PASS")
                yield json.dumps({"type": "result", "passed": True}) + "\n"
            elif line == "FLASH:FAIL":
                print("[build] RESULT: FAIL")
                yield json.dumps({"type": "result", "passed": False}) + "\n"
            else:
                print(f"[build] {line}")
                yield json.dumps({"type": "log", "line": line}) + "\n"

    return Response(generate(), mimetype="application/x-ndjson")


@app.route("/firmware/<segment>", methods=["GET"])
def firmware_segment(segment: str):
    """Serves one just-built .bin segment (bootloader/partitions/app)
    for the native app to download and flash over USB-OTG. Call /build
    first -- this just reads whatever's currently on disk, no build
    triggered here.
    """
    env = request.args.get("env", DEFAULT_ENV)
    path = flasher.firmware_segment_path(env, segment)
    if path is None:
        return jsonify({"ok": False, "error": f"unknown segment '{segment}'"}), 400
    if not path.exists():
        return jsonify({"ok": False, "error": f"{path.name} not found -- run /build first"}), 404
    return Response(path.read_bytes(), mimetype="application/octet-stream")


@app.route("/boot_log", methods=["GET"])
def boot_log():
    port = request.args.get("port")
    if not port:
        return jsonify({"ok": False, "error": "missing ?port="}), 400
    window_s = float(request.args.get("window_s", 10.0))
    try:
        result = serial_monitor.capture_boot_log(port, window_s=window_s)
    except Exception as e:  # bad/unavailable serial port, etc.
        return jsonify({"ok": False, "error": str(e)}), 500

    # Target the jig whenever a device ID was parsed at all -- independent
    # of whether the rest of the boot log passed (e.g. a real RTC-chip
    # defect shouldn't stop the jig from being pointed at the right unit
    # for the relay/flow steps that still need to run).
    jig_targeted = False
    if result.get("device_id"):
        jig_targeted = notify_jig_target(result["device_id"])

    return jsonify({"ok": True, "jig_targeted": jig_targeted, **result})


@app.route("/jig_target", methods=["GET", "POST"])
def jig_target():
    """Manually (re-)pin the jig at a specific SSID without touching the
    DUT at all -- /boot_log already does this automatically after a
    flash, this is for retrying/debugging that step in isolation.
    """
    device_id = request.values.get("device_id")
    if not device_id:
        return jsonify({"ok": False, "error": "missing ?device_id="}), 400
    ok = notify_jig_target(device_id)
    return jsonify({"ok": ok})


@app.route("/log_result", methods=["POST"])
def log_result():
    """Lets the phone app centralize results onto the laptop's CSV too
    (same file/schema results_logger.py already writes), in addition
    to whatever it keeps locally on-device.
    """
    body = request.get_json(force=True, silent=True) or {}
    device_id = body.get("device_id")
    tier = body.get("tier", "production")
    # Both the Flutter and native apps' TestReport.toJson() send
    # "overall_passed", not "passed" -- this previously read the wrong
    # key and silently logged every result as FAIL regardless of the
    # real outcome. "passed" kept as a fallback for any older caller.
    passed = bool(body.get("overall_passed", body.get("passed")))
    steps = body.get("steps", {})
    results_logger.log_result(device_id, tier, passed, steps)
    return jsonify({"ok": True})


@app.route("/results", methods=["GET"])
def results():
    """Returns recent rows from test_results.csv, most recent first --
    backs the dashboard's results table and gives the phone app a way
    to show "everything tested today on this bench", not just what it
    ran itself.
    """
    limit = int(request.args.get("limit", 50))
    path = results_logger.LOG_PATH
    if not path.exists():
        return jsonify({"ok": True, "rows": []})

    import csv
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    rows = rows[-limit:][::-1]
    for row in rows:
        try:
            row["steps"] = json.loads(row.get("steps_json", "{}"))
        except (json.JSONDecodeError, TypeError):
            row["steps"] = {}
    return jsonify({"ok": True, "rows": rows})


DASHBOARD_HTML = (Path(__file__).resolve().parent / "flash_bridge_dashboard.html")


@app.route("/", methods=["GET"])
def dashboard():
    return Response(DASHBOARD_HTML.read_text(), mimetype="text/html")


if __name__ == "__main__":
    print(f"Flash Bridge listening on http://0.0.0.0:{BRIDGE_PORT}")
    print("Open that address (with this laptop's LAN IP) in a browser for the built-in dashboard.")
    app.run(host="0.0.0.0", port=BRIDGE_PORT, threaded=True)
