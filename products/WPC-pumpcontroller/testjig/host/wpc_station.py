"""WPC Production Station -- browser UI for flashing, testing and pairing WPC Masters and Pump Nodes.

Modelled on the FG1 Flash Bridge (products/FG1-flowguard/testing/flash_bridge.py): a small Flask
service on the bench PC plus one HTML page.

    pip install flask pyserial
    python wpc_station.py            # then open http://localhost:8788/

What it does
  * Scan   -- finds USB serial boards and identifies each as Jig / Master / Pump (or blank/unknown).
  * Units  -- the operator assigns a role to each port; any number of Masters and Pumps, including 0.
  * Production mode  -- optional flash, then the wpc_test.py functional test per unit; the test ends
                        with FACTORYRESET so the unit ships clean. Every other unit is held in reset
                        during a test (a still-joined Pump answers a Master under test otherwise).
  * Customer pairing -- 1 Master + n Pumps: point every Pump at the Master, wait until all are joined
                        and online, record the pairing. NO factory reset: ship as paired.
  * Settings         -- antenna (RSSI) limit and enforce/record-only, fixture wiring flags, flash options.

Bench-network only: no authentication (same as the FG1 bridge).
"""
import csv
import datetime
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial
import serial.tools.list_ports
from flask import Flask, Response, jsonify, request

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wpc_test as w  # noqa: E402  (Device, open_port, parse_kv are reused as-is)

FIRMWARE_ROOT = HERE.parent.parent / "firmware"
FW_DIRS = {"master": FIRMWARE_ROOT / "master_node", "pump": FIRMWARE_ROOT / "pump_node"}
SETTINGS_PATH = HERE / "wpc_station_settings.json"
PAIRINGS_CSV = HERE / "wpc_pairings.csv"
STATION_PORT = 8788

DEFAULT_SETTINGS = {
    # Antenna test (RSSI of the DUT's packets as seen by the jig, averaged)
    "antenna_min_rssi_master_dbm": -90,
    "antenna_min_rssi_pump_dbm": -90,
    "antenna_enforce": True,          # False = log the value but never fail on it
    "antenna_samples": 5,
    # Fixture wiring that is not connected yet -> those steps are skipped (and shown as SKIP)
    "skip_master_in4": True,
    "skip_pump_adc": True,
    "adc_steps": "0,15,30,45",
    "adc_min_span_mv": 150,
    "adc_crosstalk_mv": 80,
    # Flashing
    "master_env": "esp32dev",
    "pump_env": "esp32dev",
    "flash_attempts": 3,
    # Testing
    "prompt_before_each_test": True,  # the fixture harness is swapped by hand between units
    "results_csv": str(HERE / "wpc_test_results.csv"),
    # Pairing
    "pair_timeout_s": 120,
}

app = Flask(__name__)


# ------------------------------------------------------------------ settings
def load_settings():
    s = dict(DEFAULT_SETTINGS)
    try:
        s.update(json.loads(SETTINGS_PATH.read_text()))
    except (OSError, ValueError):
        pass
    return s


def save_settings(new):
    s = load_settings()
    for k, default in DEFAULT_SETTINGS.items():
        if k in new:
            v = new[k]
            try:
                if isinstance(default, bool):
                    v = bool(v)
                elif isinstance(default, int):
                    v = int(float(v))
                elif isinstance(default, float):
                    v = float(v)
                else:
                    v = str(v)
            except (TypeError, ValueError):
                continue
            s[k] = v
    SETTINGS_PATH.write_text(json.dumps(s, indent=2))
    return s


# ---------------------------------------------------------------- job state
class Job:
    """One run at a time. The worker thread appends to `log` and updates `units`; the page polls."""

    def __init__(self):
        self.lock = threading.Lock()
        self.reset()

    def reset(self):
        self.running = False
        self.stop_flag = threading.Event()
        self.ack = threading.Event()
        self.mode = ""
        self.started = None
        self.finished = None
        self.log = []
        self.units = {}          # port -> {role, label, stage, steps[], verdict, note}
        self.prompt = None       # {"text":..., "port":...} while waiting for the operator
        self.summary = None
        self.proc = None

    def say(self, line):
        line = line.rstrip()
        if line:
            self.log.append(line)
            print(line, flush=True)


JOB = Job()
BUSY = threading.Lock()   # held for the whole run, and by the scanner


class Stopped(Exception):
    pass


def check_stop():
    if JOB.stop_flag.is_set():
        raise Stopped()


# ------------------------------------------------------------- serial helpers
def candidate_ports():
    out = []
    for p in serial.tools.list_ports.comports():
        name = p.device
        low = name.lower()
        if "bluetooth" in low or "debug-console" in low:
            continue
        if p.vid is None and not low.startswith(("com", "/dev/ttyusb", "/dev/ttyacm")):
            continue
        if low.startswith("/dev/tty.") and sys.platform == "darwin":
            continue   # use the cu.* twin
        out.append(name)
    return sorted(out)


def reset_pulse(p):
    """Explicit EN pulse via RTS (works whether or not the OS already reset the board on open)."""
    try:
        p.dtr = False
        p.rts = True
        time.sleep(0.15)
        p.rts = False
    except Exception:
        pass


def open_dev(name, port, pulse=True):
    p = w.open_port(port)
    if pulse:
        reset_pulse(p)
    return w.Device(name, p)


def identify(port, total=10.0):
    """Reset the board on `port` and ask it for ID. Returns a dict for the Units table."""
    info = {"port": port, "kind": "unknown", "id": "", "mac": "", "fw": "", "note": ""}
    dev = None
    try:
        dev = open_dev("scan", port)
        r = dev.wait_ready(total)
        kv = r.kv
        board = kv.get("board", "")
        info.update(mac=kv.get("mac", ""), fw=kv.get("fw", ""))
        if board == "WPC-JIG":
            info["kind"] = "jig"
        elif board == "WPC-MASTER":
            info.update(kind="master", id=kv.get("masterId", ""))
        elif board == "WPC-PUMP":
            info.update(kind="pump", id=kv.get("pumpId", ""))
        else:
            info["note"] = r.text[:60]
    except TimeoutError:
        info["note"] = "no answer (blank / no WPC firmware / not a WPC board)"
    except Exception as e:
        info["kind"] = "busy"
        info["note"] = f"cannot open: {e}"
    finally:
        try:
            dev and dev.port.close()
        except Exception:
            pass
    return info


class Holder:
    """Keeps boards in reset (RTS asserted = EN low) while another unit is being tested."""

    def __init__(self, ports):
        self.ports = []
        for path in ports:
            try:
                p = serial.Serial()
                p.port = path
                p.baudrate = 115200
                p.dtr = False
                p.rts = True
                p.open()
                self.ports.append(p)
            except Exception as e:
                JOB.say(f"  ! could not hold {path} in reset: {e}")

    def release(self):
        for p in self.ports:
            try:
                p.rts = False
                p.close()
            except Exception:
                pass
        self.ports = []


# --------------------------------------------------------------------- flash
def filtered(line):
    """PlatformIO prints a line per few KB while writing; keep the log readable."""
    if "Writing at 0x" in line and "(100 %)" not in line:
        return False
    return True


def flash_unit(port, role, s):
    env = s["master_env"] if role == "master" else s["pump_env"]
    fwdir = FW_DIRS[role]
    if shutil.which("pio") is None:
        JOB.say("  ! PlatformIO ('pio') not found on PATH")
        return False, "pio not found"
    attempts = max(1, int(s["flash_attempts"]))
    for n in range(1, attempts + 1):
        check_stop()
        JOB.say(f"  flash {role} on {port}  (env {env}, attempt {n}/{attempts})")
        cmd = ["pio", "run", "-e", env, "-t", "upload", "--upload-port", port]
        proc = subprocess.Popen(cmd, cwd=fwdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        JOB.proc = proc
        for line in proc.stdout:
            if filtered(line):
                JOB.say("    " + line)
            if JOB.stop_flag.is_set():
                proc.terminate()
                raise Stopped()
        rc = proc.wait()
        JOB.proc = None
        # The REAL exit status decides -- never a grep on the output.
        if rc == 0:
            return True, f"flashed on attempt {n}"
        JOB.say(f"  ! upload failed (exit {rc})")
        time.sleep(2)
    return False, f"upload failed after {attempts} attempts"


def verify_role(port, role):
    """After flashing: the board must come up answering as the right kind of unit."""
    info = identify(port, total=25.0)
    ok = info["kind"] == role
    return ok, info


# ---------------------------------------------------------------------- test
STEP_RE = re.compile(r"^\s*\[(PASS|FAIL|SKIP)\]\s+(.*?)(?:\s+--\s+(.*))?$")


def test_flags(role, s):
    f = []
    limit = s["antenna_min_rssi_master_dbm"] if role == "master" else s["antenna_min_rssi_pump_dbm"]
    f += ["--min-rssi", str(limit), "--rssi-samples", str(s["antenna_samples"])]
    if not s["antenna_enforce"]:
        f.append("--rssi-record-only")
    if role == "master" and s["skip_master_in4"]:
        f.append("--skip-in4")
    if role == "pump":
        if s["skip_pump_adc"]:
            f.append("--skip-adc")
        else:
            f += ["--adc-steps", str(s["adc_steps"]), "--adc-min-span", str(s["adc_min_span_mv"]),
                  "--adc-crosstalk", str(s["adc_crosstalk_mv"])]
    return f


def test_unit(port, role, jig_port, others, s):
    """Run wpc_test.py for one DUT. Returns (passed, [steps])."""
    u = JOB.units[port]
    steps = u["steps"]
    holder = Holder(others)
    try:
        time.sleep(1.0)
        cmd = [sys.executable, "-u", str(HERE / "wpc_test.py"), role, "--jig", jig_port, "--dut", port,
               "--csv", s["results_csv"]] + test_flags(role, s)
        JOB.say(f"  test {role} on {port}: {' '.join(cmd[3:])}")
        proc = subprocess.Popen(cmd, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        JOB.proc = proc
        verdict = None
        for line in proc.stdout:
            line = line.rstrip()
            m = STEP_RE.match(line)
            if m:
                steps.append({"status": m.group(1), "name": m.group(2), "detail": m.group(3) or ""})
            elif line.startswith("==="):
                verdict = "PASS" if "PASS" in line else "FAIL"
            JOB.say("    " + line)
            if JOB.stop_flag.is_set():
                proc.terminate()
                raise Stopped()
        rc = proc.wait()
        JOB.proc = None
        return (rc == 0 and verdict == "PASS"), steps
    finally:
        holder.release()


# ------------------------------------------------------------------- pairing
def pair_customer(master, pumps, s, clear_table):
    """Point every Pump at the Master, wait until all are joined and online. NO factory reset."""
    m_dev = open_dev("master", master)
    p_devs = [(p, open_dev(f"pump@{p}", p)) for p in pumps]
    try:
        m_info = m_dev.wait_ready(30).kv
        master_id = m_info["masterId"]
        JOB.say(f"  Master {master_id} (mac {m_info['mac']}, fw {m_info['fw']})")
        p_info = {}
        for port, d in p_devs:
            p_info[port] = d.wait_ready(30).kv
            JOB.say(f"  Pump {p_info[port]['pumpId']} (mac {p_info[port]['mac']}, fw {p_info[port]['fw']}) on {port}")
            JOB.units[port]["label"] = f"Pump {p_info[port]['pumpId']}"
        JOB.units[master]["label"] = f"Master {master_id}"
        m_dev.ok("TESTMODE 0")            # normal timings; never ship in test mode
        for _, d in p_devs:
            d.ok("TESTMODE 0")
        check_stop()

        if clear_table:
            m_dev.ok("FORGETALL")
            JOB.say("  Master pump table cleared (only this customer's pumps will be paired)")
        for port, d in p_devs:
            d.ok(f"MASTER {master_id}")
            JOB.say(f"  Pump {p_info[port]['pumpId']} -> Master {master_id}")

        timeout = float(s["pair_timeout_s"])
        end = time.time() + timeout
        want = {p_info[port]["pumpId"]: port for port, _ in p_devs}
        joined = set()
        online = set()
        while time.time() < end and online != set(want):
            check_stop()
            for port, d in p_devs:
                pid = p_info[port]["pumpId"]
                if pid not in joined:
                    st = d.ok("STATE").kv
                    if st.get("joined") == "1":
                        joined.add(pid)
                        JOB.say(f"  Pump {pid} joined (slot {st.get('slot')})")
                        JOB.units[port]["steps"].append({"status": "PASS", "name": "joined the Master", "detail": f"slot {st.get('slot')}"})
            table = [w.parse_kv(x) for x in m_dev.ok("PUMPS").data]
            for row in table:
                if row.get("pumpId") in want and row.get("online") == "1" and row["pumpId"] not in online:
                    online.add(row["pumpId"])
                    JOB.say(f"  Master sees Pump {row['pumpId']} online")
                    JOB.units[want[row["pumpId"]]]["steps"].append({"status": "PASS", "name": "Master reports it online", "detail": f"slot {row.get('slot')}"})
            time.sleep(2.0)

        table = [w.parse_kv(x) for x in m_dev.ok("PUMPS").data]
        extra = [r["pumpId"] for r in table if r.get("pumpId") not in want]
        ok = online == set(want)
        for pid, port in want.items():
            if pid not in online:
                JOB.units[port]["steps"].append({"status": "FAIL", "name": "joined and online at the Master",
                                                 "detail": f"not online after {timeout:.0f}s (joined={pid in joined})"})
        if extra:
            JOB.say(f"  note: Master also still lists other pumps: {', '.join(extra)}"
                    + ("" if clear_table else " (table was not cleared)"))
        return ok, master_id, m_info["mac"], [
            {"pumpId": p_info[port]["pumpId"], "mac": p_info[port]["mac"], "online": p_info[port]["pumpId"] in online}
            for port, _ in p_devs], extra
    finally:
        for _, d in p_devs:
            try:
                d.port.close()
            except Exception:
                pass
        try:
            m_dev.port.close()
        except Exception:
            pass


def log_pairing(customer, order, master_id, master_mac, pumps, ok):
    new = not PAIRINGS_CSV.exists()
    with open(PAIRINGS_CSV, "a", newline="") as f:
        wr = csv.writer(f)
        if new:
            wr.writerow(["timestamp", "customer", "order", "master_id", "master_mac", "pumps_json", "verdict"])
        wr.writerow([datetime.datetime.now().isoformat(timespec="seconds"), customer, order, master_id, master_mac,
                     json.dumps(pumps), "PASS" if ok else "FAIL"])


# ------------------------------------------------------------------ the run
def run_job(req):
    s = load_settings()
    mode = req.get("mode", "production")
    units = [u for u in req.get("units", []) if u.get("role") in ("master", "pump", "jig")]
    masters = [u["port"] for u in units if u["role"] == "master"]
    pumps = [u["port"] for u in units if u["role"] == "pump"]
    jigs = [u["port"] for u in units if u["role"] == "jig"]
    do_flash = bool(req.get("flash"))
    do_test = bool(req.get("test"))
    try:
        JOB.say(f"=== {mode.upper()} run: {len(masters)} Master(s), {len(pumps)} Pump(s) ===")
        results = []

        # -------- optional flash (sequential; the jig is never flashed from here)
        if do_flash:
            for port in masters + pumps:
                role = "master" if port in masters else "pump"
                u = JOB.units[port]
                u["stage"] = "flashing"
                ok, note = flash_unit(port, role, s)
                if ok:
                    ok, info = verify_role(port, role)
                    note = (f"flashed, boots as {info['kind']} fw {info['fw']}" if ok
                            else f"flashed but answers as '{info['kind']}' ({info['note']})")
                u["steps"].append({"status": "PASS" if ok else "FAIL", "name": "flash + boot check", "detail": note})
                if not ok:
                    u["verdict"] = "FAIL"
                    u["stage"] = "done"
                JOB.say(f"  {'OK' if ok else 'FAILED'}: {note}")

        # -------- production test
        if mode == "production" and do_test:
            if not jigs:
                raise RuntimeError("Testing needs the Jig: assign one port the role 'Jig' (Scan finds it).")
            if len(jigs) > 1:
                raise RuntimeError("More than one port is assigned as Jig.")
            for port in masters + pumps:
                role = "master" if port in masters else "pump"
                u = JOB.units[port]
                if u["verdict"] == "FAIL":
                    JOB.say(f"  skipping test of {port}: flash failed")
                    continue
                check_stop()
                if s["prompt_before_each_test"]:
                    JOB.ack.clear()
                    JOB.prompt = {"port": port, "text": f"Connect the {role.upper()} on {port} to the test fixture "
                                  f"({'level/No-Power' if role == 'master' else 'relay/analog'} harness), then press Ready."}
                    u["stage"] = "waiting for operator"
                    JOB.say(f"  waiting for operator: connect {role} {port}")
                    while not JOB.ack.wait(0.5):
                        check_stop()
                    JOB.prompt = None
                u["stage"] = "testing"
                others = [p for p in masters + pumps if p != port]
                passed, steps = test_unit(port, role, jigs[0], others, s)
                u["verdict"] = "PASS" if passed else "FAIL"
                u["stage"] = "done"
                JOB.say(f"  {role} {port}: {u['verdict']}")

        # -------- customer pairing
        if mode == "pairing":
            if len(masters) != 1 or not pumps:
                raise RuntimeError("Pairing needs exactly one Master and at least one Pump.")
            for u in (JOB.units[p] for p in masters + pumps):
                u["stage"] = "pairing"
            ok, master_id, master_mac, pump_list, extra = pair_customer(
                masters[0], pumps, s, bool(req.get("clear_master_table", True)))
            for p in masters + pumps:
                JOB.units[p]["verdict"] = "PASS" if ok else "FAIL"
                JOB.units[p]["stage"] = "done"
            log_pairing(req.get("customer", ""), req.get("order", ""), master_id, master_mac, pump_list, ok)
            JOB.summary = {"pairing": {"ok": ok, "master_id": master_id, "pumps": [p["pumpId"] for p in pump_list]}}
            JOB.say(f"=== PAIRING {'OK' if ok else 'FAILED'}: Master {master_id} <- Pump(s) "
                    f"{', '.join(p['pumpId'] for p in pump_list)}. Units left as paired (no factory reset). ===")

        for u in JOB.units.values():
            if u["stage"] != "done" and u["role"] != "jig":
                u["stage"] = "done"
                u["verdict"] = u["verdict"] or ("PASS" if any(x["status"] == "PASS" for x in u["steps"]) else "")
    except Stopped:
        JOB.say("=== STOPPED by operator ===")
    except Exception as e:
        JOB.say(f"=== ERROR: {e} ===")
    finally:
        JOB.prompt = None
        JOB.running = False
        JOB.finished = time.time()
        BUSY.release()


# ---------------------------------------------------------------------- API
@app.after_request
def cors(resp):
    resp.headers["Access-Control-Allow-Origin"] = "*"
    return resp


def lan_ip():
    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sk.connect(("8.8.8.8", 80))
        return sk.getsockname()[0]
    except OSError:
        return None
    finally:
        sk.close()


@app.route("/api/health")
def health():
    return jsonify({"ok": True, "pio_found": shutil.which("pio") is not None, "lan_ip": lan_ip(),
                    "port": STATION_PORT, "busy": JOB.running})


@app.route("/api/settings", methods=["GET", "POST"])
def settings():
    if request.method == "POST":
        return jsonify(save_settings(request.get_json(force=True, silent=True) or {}))
    return jsonify(load_settings())


@app.route("/api/scan", methods=["POST"])
def scan():
    if not BUSY.acquire(blocking=False):
        return jsonify({"ok": False, "error": "a run is in progress"}), 409
    try:
        ports = candidate_ports()
        results = [None] * len(ports)

        def work(i, p):
            results[i] = identify(p)
        threads = [threading.Thread(target=work, args=(i, p)) for i, p in enumerate(ports)]
        [t.start() for t in threads]
        [t.join() for t in threads]
        return jsonify({"ok": True, "ports": results})
    finally:
        BUSY.release()


@app.route("/api/run", methods=["POST"])
def run():
    req = request.get_json(force=True, silent=True) or {}
    if not BUSY.acquire(blocking=False):
        return jsonify({"ok": False, "error": "a run is already in progress"}), 409
    JOB.reset()
    JOB.running = True
    JOB.mode = req.get("mode", "production")
    JOB.started = time.time()
    for u in req.get("units", []):
        if u.get("role") in ("master", "pump", "jig"):
            JOB.units[u["port"]] = {"role": u["role"], "label": u.get("label") or u["role"].title(),
                                    "stage": "queued" if u["role"] != "jig" else "jig", "steps": [],
                                    "verdict": "", "note": ""}
    threading.Thread(target=run_job, args=(req,), daemon=True).start()
    return jsonify({"ok": True})


@app.route("/api/job")
def job():
    since = int(request.args.get("since", 0))
    return jsonify({"running": JOB.running, "mode": JOB.mode, "units": JOB.units, "prompt": JOB.prompt,
                    "summary": JOB.summary, "log": JOB.log[since:], "next": len(JOB.log)})


@app.route("/api/ack", methods=["POST"])
def ack():
    JOB.ack.set()
    return jsonify({"ok": True})


@app.route("/api/stop", methods=["POST"])
def stop():
    JOB.stop_flag.set()
    JOB.ack.set()
    if JOB.proc:
        try:
            JOB.proc.terminate()
        except Exception:
            pass
    return jsonify({"ok": True})


def read_csv(path, limit):
    p = Path(path)
    if not p.exists():
        return []
    with open(p, newline="") as f:
        rows = list(csv.DictReader(f))
    return rows[-limit:][::-1]


@app.route("/api/results")
def results():
    limit = int(request.args.get("limit", 40))
    rows = read_csv(load_settings()["results_csv"], limit)
    for r in rows:
        try:
            r["steps"] = json.loads(r.get("results_json", "[]"))
        except ValueError:
            r["steps"] = []
        r.pop("results_json", None)
    return jsonify({"ok": True, "rows": rows})


@app.route("/api/pairings")
def pairings():
    rows = read_csv(PAIRINGS_CSV, int(request.args.get("limit", 40)))
    for r in rows:
        try:
            r["pumps"] = json.loads(r.get("pumps_json", "[]"))
        except ValueError:
            r["pumps"] = []
        r.pop("pumps_json", None)
    return jsonify({"ok": True, "rows": rows})


@app.route("/")
def page():
    return Response((HERE / "wpc_station.html").read_text(), mimetype="text/html")


if __name__ == "__main__":
    print(f"WPC Production Station: http://localhost:{STATION_PORT}/   (LAN: http://{lan_ip()}:{STATION_PORT}/)")
    app.run(host="0.0.0.0", port=STATION_PORT, threaded=True)
