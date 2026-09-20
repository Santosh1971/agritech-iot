#!/usr/bin/env python3
"""WPC factory test -- host side.

Drives a test jig (WM1 board running testjig/firmware) and a device under
test (DUT: a WPC Master or a Pump Node) over two serial ports, e.g. both on
the same USB hub, and prints/records PASS/FAIL per check.

    python wpc_test.py master --jig /dev/cu.usbserial-A --dut /dev/cu.usbserial-B
    python wpc_test.py pump   --jig /dev/cu.usbserial-A --dut /dev/cu.usbserial-C
    python wpc_test.py master --simulate          # no hardware: built-in simulator

Requires pyserial (pip install pyserial). The optional cloud check also needs
paho-mqtt. Fixture-dependent limits (RSSI, ADC steps) are command-line options
because they depend on how the jig is wired; the defaults are starting points.

Serial protocol (both jig and DUT firmware): one command per line, replies are
lines starting with '@':  @OK k=v ...  |  @ERR reason  |  @DATA ...  |  @EVT ...
Everything else the firmware prints (its normal [TAG] log) is ignored.
"""
import argparse
import csv
import datetime
import json
import os
import re
import sys
import time

BAUD = 115200
TEST_MASTER_ID = "7E570001"      # Master ID the jig pretends to be when testing a Pump
TEST_PUMP_ID = 9999              # Pump ID the jig pretends to be when testing a Master


# ---------------------------------------------------------------- device I/O
class Reply:
    def __init__(self, ok, text, data):
        self.ok = ok
        self.text = text
        self.data = data
        self.kv = parse_kv(text)

    def __repr__(self):
        return ("OK " if self.ok else "ERR ") + self.text


def parse_kv(text):
    out = {}
    for tok in text.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


PROTO_RE = re.compile(r"@(OK|ERR|DATA|EVT)\b")
# Commands that end in a reboot: no reliable reply, and repeating them would be wrong.
NO_RETRY = ("FACTORYRESET", "REBOOT")


class Device:
    def __init__(self, name, port):
        self.name = name
        self.port = port
        self.log = []          # non-protocol lines, kept for debugging
        self.events = []       # @EVT lines
        self._partial = b""

    def _line(self):
        """One complete line. pyserial's readline() returns whatever it has when its
        timeout expires, so a line split across USB packets can come back in pieces --
        glue them back together instead of treating a fragment as a line."""
        raw = self.port.readline()
        if not raw:
            return None
        raw = self._partial + raw
        if not raw.endswith(b"\n"):
            self._partial = raw
            return None
        self._partial = b""
        return raw.decode(errors="replace").strip()

    def _cmd_once(self, line, timeout):
        try:
            self.port.reset_input_buffer()
        except Exception:
            pass
        self._partial = b""
        self.port.write((line + "\n").encode())
        deadline = time.time() + timeout
        data = []
        while time.time() < deadline:
            text = self._line()
            if not text:
                continue
            # The console output is occasionally preceded by UART garbage on the same
            # line; look for the protocol tag rather than requiring it at column 0.
            m = PROTO_RE.search(text)
            if not m:
                self.log.append(text)
                continue
            tag = m.group(1)
            rest = text[m.end():].strip()
            if tag == "DATA":
                data.append(rest)
            elif tag == "EVT":
                self.events.append(rest)
            else:
                return Reply(tag == "OK", rest, data)
        raise TimeoutError(f"{self.name}: no reply to '{line}'")

    def cmd(self, line, timeout=4.0):
        """Send one command. A reply can be lost to serial corruption, so a timeout is
        retried once -- except for commands that reboot the board."""
        retry = 0 if line.upper().startswith(NO_RETRY) else 1
        for attempt in range(retry + 1):
            try:
                return self._cmd_once(line, timeout)
            except TimeoutError:
                if attempt == retry:
                    raise

    def ok(self, line, timeout=4.0):
        r = self.cmd(line, timeout)
        if not r.ok:
            raise RuntimeError(f"{self.name}: '{line}' -> {r}")
        return r

    def wait_ready(self, total=30.0):
        """A freshly (re)booted board needs a few seconds; poll ID until it answers."""
        end = time.time() + total
        last = None
        while time.time() < end:
            try:
                r = self._cmd_once("ID", 1.2)
                if r.ok:
                    return r
            except TimeoutError as e:
                last = e
            time.sleep(0.5)
        raise TimeoutError(f"{self.name}: not ready after {total}s ({last})")


def open_port(path):
    if path.startswith("sim:"):
        from sim_devices import make_sim_port
        return make_sim_port(path)
    import serial
    p = serial.Serial()
    p.port = path
    p.baudrate = BAUD
    p.timeout = 0.2
    # NOTE: on macOS opening the port resets these boards even with DTR/RTS low, so every
    # run starts from a fresh boot; wait_ready() below absorbs the boot time.
    p.dtr = False
    p.rts = False
    p.open()
    return p


def wait_until(fn, timeout, interval=0.3):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(interval)
    return None


# ------------------------------------------------------------------- report
class Report:
    def __init__(self):
        self.rows = []

    def check(self, name, passed, detail=""):
        self.rows.append((name, bool(passed), str(detail)))
        print(f"  [{'PASS' if passed else 'FAIL'}] {name}" + (f"  -- {detail}" if detail else ""))
        return bool(passed)

    def skip(self, name, why):
        self.rows.append((name, None, why))
        print(f"  [SKIP] {name}  -- {why}")

    @property
    def passed(self):
        return all(r[1] for r in self.rows if r[1] is not None) and any(r[1] for r in self.rows)


# ------------------------------------------------------------- master tests
def pumps_table(dut):
    r = dut.ok("PUMPS")
    return [parse_kv(d) for d in r.data]


def find_pump(dut, pump_id):
    for p in pumps_table(dut):
        if p.get("pumpId") == str(pump_id):
            return p
    return None


def jig_pump_status(jig):
    return jig.ok("PUMPEMU STATUS").kv


def wait_cmd(jig, state, timeout, min_cmds=None):
    """Wait for the jig (playing pump) to receive a LEVEL_CMD carrying `state` newer than min_cmds."""
    def f():
        s = jig_pump_status(jig)
        if int(num(s.get("lastCmd"), -1)) == state and (min_cmds is None or int(num(s.get("cmds"))) > min_cmds):
            return s
        return None
    return wait_until(f, timeout)


def test_master(dut, jig, args, rpt):
    info = dut.wait_ready().kv
    master_id = info["masterId"]
    print(f"DUT: WPC Master  mac={info['mac']}  masterId={master_id}  fw={info['fw']}")
    jig.ok("RESET")
    rpt.check("DUT responds with ID / firmware version", True, f"fw={info['fw']}")

    dut.cmd("FACTORYRESET")            # known state: empty pump table, default config
    time.sleep(1.0)
    dut.wait_ready()
    dut.ok("TESTMODE 1")               # 300ms level debounce, 1s poll gap -- test timing only

    # 1. level inputs: RL1..RL4 close a contact to GND on the Master's IN1..IN4
    ok = True
    detail = []
    for i in range(1, 5):
        jig.ok(f"RELAY {i} 1")
        time.sleep(0.15)
        raw = dut.ok("INPUTS").kv["raw"].split(",")
        good = raw[i - 1] == "1" and all(v == "0" for j, v in enumerate(raw) if j != i - 1)
        ok &= good
        detail.append(f"IN{i}:{''.join(raw)}")
        jig.ok(f"RELAY {i} 0")
    rpt.check("float inputs IN1-IN4 read correctly (each isolated)", ok, " ".join(detail))

    # 2. LoRa join: the jig plays a Pump Node
    jig.ok(f"PUMPEMU START {master_id} {TEST_PUMP_ID}")
    joined = wait_until(lambda: jig_pump_status(jig).get("joined") == "1", args.join_timeout)
    rpt.check("Master accepts a LoRa JOIN_REQUEST and replies JOIN_ACCEPT", joined)
    if not joined:
        return
    pump = find_pump(dut, TEST_PUMP_ID)
    rpt.check("Master registered the pump in its table", pump is not None)
    if pump is None:
        return
    slot = pump["slot"]

    # 3. level logic: assigned level open -> pump must be commanded ON; closed -> OFF
    for lvl in (1, 2, 3):
        dut.ok(f"ASSIGN {slot} {1 << (lvl - 1)}")
        jig.ok("RELAYS 0")
        s = wait_cmd(jig, 1, args.cmd_timeout)
        on_ok = s is not None
        n = int(num(jig_pump_status(jig).get("cmds")))
        jig.ok(f"RELAY {lvl} 1")
        s = wait_cmd(jig, 0, args.cmd_timeout, min_cmds=n)
        off_ok = s is not None
        # closing a level this pump is NOT assigned to must not turn it off
        jig.ok("RELAYS 0")
        wait_cmd(jig, 1, args.cmd_timeout, min_cmds=n)
        n = int(num(jig_pump_status(jig).get("cmds")))
        other = (lvl % 3) + 1
        jig.ok(f"RELAY {other} 1")
        time.sleep(args.cmd_timeout / 2)
        stayed = int(num(jig_pump_status(jig).get("lastCmd"), -1)) == 1
        jig.ok("RELAYS 0")
        rpt.check(f"level {lvl}: open->ON, closed->OFF, other levels ignored", on_ok and off_ok and stayed,
                  f"on={on_ok} off={off_ok} ignoredOther={stayed}")

    # 4. manual override beats level logic
    dut.ok(f"ASSIGN {slot} 1")
    jig.ok("RELAY 1 1")                                   # level says OFF
    wait_cmd(jig, 0, args.cmd_timeout)
    n = int(num(jig_pump_status(jig).get("cmds")))
    dut.ok(f"OVERRIDE {slot} on")
    forced_on = wait_cmd(jig, 1, args.cmd_timeout, min_cmds=n) is not None
    n = int(num(jig_pump_status(jig).get("cmds")))
    dut.ok(f"OVERRIDE {slot} off")
    jig.ok("RELAY 1 0")                                   # level says ON
    forced_off = wait_cmd(jig, 0, args.cmd_timeout, min_cmds=n) is not None
    n = int(num(jig_pump_status(jig).get("cmds")))
    dut.ok(f"OVERRIDE {slot} auto")
    back_auto = wait_cmd(jig, 1, args.cmd_timeout, min_cmds=n) is not None
    rpt.check("manual override ON/OFF overrides level logic, AUTO restores it",
              forced_on and forced_off and back_auto,
              f"on={forced_on} off={forced_off} auto={back_auto}")

    # 5. telemetry piggybacked on CMD_ACK
    jig.ok("PUMPEMU ADC 1234 567")
    got = wait_until(lambda: (lambda p: p and p.get("adc1") == "1234" and p.get("adc4") == "567")(find_pump(dut, TEST_PUMP_ID)),
                     args.cmd_timeout * 2)
    rpt.check("IN1/IN4 ADC values from CMD_ACK reach the Master", got)

    # 6. radio link quality as seen by the jig
    s = jig_pump_status(jig)
    rssi = num(s.get("rssi"), -999)
    rpt.check(f"Master TX signal strength at jig >= {args.min_rssi} dBm", rssi >= args.min_rssi,
              f"rssi={rssi} snr={s.get('snr')}")

    # 7. offline detection and recovery
    jig.ok("PUMPEMU NOACK 1")
    off = wait_until(lambda: (lambda p: p and p.get("online") == "0")(find_pump(dut, TEST_PUMP_ID)), args.offline_timeout)
    jig.ok("PUMPEMU NOACK 0")
    back = wait_until(lambda: (lambda p: p and p.get("online") == "1")(find_pump(dut, TEST_PUMP_ID)), args.offline_timeout)
    rpt.check("pump marked offline when ACKs stop, online again when they resume", off and back,
              f"offline={bool(off)} recovered={bool(back)}")

    # 8. NVS persistence of the pump table across a reboot
    dut.ok(f"ASSIGN {slot} 5")
    dut.cmd("REBOOT")
    time.sleep(1.0)
    dut.wait_ready()
    p = find_pump(dut, TEST_PUMP_ID)
    rpt.check("pump table + level assignment survive a reboot (NVS)", p is not None and p.get("levels") == "5",
              f"after reboot: {p}")

    # 9. optional: SoftAP + HTTP API via the jig's WiFi
    if args.wifi_test:
        ap = f"WPC-Master-{master_id}"
        found = jig.ok(f"WIFISCAN {ap}", 20).kv.get("found") == "1"
        rpt.check("Master SoftAP is visible", found)
        if found:
            c = jig.cmd(f"WIFICONNECT {ap}", 25)
            rpt.check("jig joins the Master SoftAP", c.ok, c.text)
            if c.ok:
                g = jig.cmd("HTTPGET /status", 10)
                body = g.data[0] if g.data else ""
                try:
                    j = json.loads(body)
                    rpt.check("GET /status returns this Master's ID", j.get("masterId", "").upper().endswith(master_id.upper()),
                              j.get("masterId"))
                except ValueError:
                    rpt.check("GET /status returns valid JSON", False, body[:80])
            jig.cmd("WIFIDISCONNECT")
    else:
        rpt.skip("SoftAP / HTTP API", "run with --wifi-test")

    # 10. optional: cloud (office WiFi + MQTT) round trip
    if args.office_ssid:
        test_cloud(dut, jig, master_id, slot, args, rpt)
    else:
        rpt.skip("cloud (WiFi STA + MQTT)", "run with --office-ssid/--office-pass")

    dut.ok("FORGETALL")
    dut.cmd("FACTORYRESET")            # ship the unit clean
    jig.ok("RESET")


def test_cloud(dut, jig, master_id, slot, args, rpt):
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        rpt.skip("cloud (WiFi STA + MQTT)", "paho-mqtt not installed")
        return
    dut.ok(f"WIFI {args.office_ssid} {args.office_pass}")
    up = wait_until(lambda: dut.ok("WIFISTAT").kv.get("mqtt") == "1", 60, 2)
    rpt.check("Master joins office WiFi and connects to the MQTT broker", up, dut.ok("WIFISTAT").text)
    if not up:
        return
    topic = f"agrisense/WPC/WPC_{master_id}/"
    got = {}

    def on_msg(c, u, m):
        if m.topic.endswith("/status"):
            try:
                got["status"] = json.loads(m.payload.decode())
            except ValueError:
                pass

    c = mqtt.Client()
    c.username_pw_set(args.mqtt_user, args.mqtt_pass)
    c.on_message = on_msg
    try:
        c.connect(args.mqtt_host, 1883, 30)
    except Exception as e:
        rpt.check("test PC connects to the broker", False, e)
        return
    c.subscribe(topic + "status")
    c.loop_start()
    rpt.check("Master status is published (retained) to the cloud",
              wait_until(lambda: "status" in got, 15) is not None)
    n = int(num(jig_pump_status(jig).get("cmds")))
    c.publish(topic + "command", json.dumps({"cmd": "override", "slot": int(slot), "enabled": True, "state": True}))
    fired = wait_cmd(jig, 1, 15, min_cmds=n) is not None
    c.publish(topic + "command", json.dumps({"cmd": "override", "slot": int(slot), "enabled": False}))
    c.loop_stop()
    c.disconnect()
    rpt.check("remote MQTT override command reaches the pump over LoRa", fired)
    dut.ok("WIFI CLEAR")


# --------------------------------------------------------------- pump tests
def test_pump(dut, jig, args, rpt):
    info = dut.wait_ready().kv
    print(f"DUT: WPC Pump Node  mac={info['mac']}  pumpId={info['pumpId']}  fw={info['fw']}")
    jig.ok("RESET")
    rpt.check("DUT responds with ID / firmware version", True, f"fw={info['fw']}")

    dut.cmd("FACTORYRESET")
    time.sleep(1.0)
    dut.wait_ready()
    dut.ok("TESTMODE 1")               # 8s fail-safe instead of 60s

    # 1. LEDs (visual, operator-confirmed only when asked)
    if args.visual:
        dut.ok("LEDTEST 3000", 8)
        ans = input("  Did ALL Pump Node LEDs light? [y/N] ").strip().lower()
        rpt.check("all LEDs light (operator)", ans == "y")
    else:
        rpt.skip("LED check", "run with --visual for an operator prompt")

    # 2. relay hardware: driver + dry contact, sensed by the jig
    ok = True
    for state in (1, 0, 1, 0):
        dut.ok(f"RELAY {state}")
        time.sleep(0.2)
        ok &= jig.ok("SENSE").kv.get("contact") == str(state)
    rpt.check("relay closes/opens the dry contact (jig senses it)", ok)

    # 3. analog inputs: sweep each channel on its own, the other must not move
    def adc():
        return dut.ok("ADC").kv

    def sweep(ch, other):
        jig.ok(f"AOUT {other} 0")
        vals = []
        for duty in args.adc_steps:
            jig.ok(f"AOUT {ch} {duty}")
            time.sleep(0.35)
            vals.append(num(adc()[f"in{1 if ch == 1 else 4}mv"]))
        other_mv = num(adc()[f"in{4 if ch == 1 else 1}mv"])
        return vals, other_mv

    for ch, other, name in ((1, 2, "IN1"), (2, 1, "IN4")):
        jig.ok("AOUT 1 0")
        jig.ok("AOUT 2 0")
        time.sleep(0.4)
        base = num(adc()[f"in{4 if ch == 1 else 1}mv"])
        vals, other_mv = sweep(ch, other)
        rising = all(b >= a for a, b in zip(vals, vals[1:])) and (vals[-1] - vals[0]) >= args.adc_min_span
        isolated = abs(other_mv - base) <= args.adc_crosstalk
        rpt.check(f"{name} analog input tracks the jig output and is isolated from the other channel",
                  rising and isolated, f"mV={vals} otherChannelDrift={other_mv - base:+.0f}")
    jig.ok("AOUT 1 0")
    jig.ok("AOUT 2 0")

    # 4. LoRa link: the jig plays the Master
    jig.ok(f"MASTEREMU START {TEST_MASTER_ID}")
    dut.ok(f"MASTER {TEST_MASTER_ID}")
    dut.wait_ready(10)
    joined = wait_until(lambda: jig.ok("MASTEREMU STATUS").kv.get("joined") == "1", args.join_timeout)
    rpt.check("Pump sends JOIN_REQUEST and jig(Master) accepts it", joined)
    if not joined:
        return
    joined_dut = wait_until(lambda: dut.ok("STATE").kv.get("joined") == "1", 10)
    rpt.check("Pump accepts JOIN_ACCEPT", joined_dut, dut.ok("STATE").text)

    jig.ok("AOUT 1 20")
    jig.ok("AOUT 2 30")
    time.sleep(0.5)
    for state in (1, 0):
        r = jig.cmd(f"MASTEREMU CMD {state}", 12)
        acked = r.ok and r.kv.get("relay") == str(state)
        time.sleep(0.2)
        contact = jig.ok("SENSE").kv.get("contact") == str(state)
        rpt.check(f"LEVEL_CMD {'ON' if state else 'OFF'}: Pump ACKs and relay follows",
                  acked and contact, f"ack={r} contact={contact}")
        rssi = num(r.kv.get("rssi"), -999)
        rpt.check(f"Pump TX signal strength at jig >= {args.min_rssi} dBm", rssi >= args.min_rssi,
                  f"rssi={rssi} snr={r.kv.get('snr')}")
        if state == 1:
            a = adc()
            drift1 = abs(num(r.kv.get("adc1")) - num(a["in1raw"]))
            drift4 = abs(num(r.kv.get("adc4")) - num(a["in4raw"]))
            rpt.check("ADC values inside CMD_ACK match the Pump's own reading",
                      drift1 <= 150 and drift4 <= 150, f"ack=({r.kv.get('adc1')},{r.kv.get('adc4')}) local=({a['in1raw']},{a['in4raw']})")

    # 5. fail-safe: relay must drop by itself when the Master goes quiet
    jig.cmd("MASTEREMU CMD 1", 12)
    contact_on = jig.ok("SENSE").kv.get("contact") == "1"
    dropped = wait_until(lambda: jig.ok("SENSE").kv.get("contact") == "0", 20, 0.5)
    fs = int(num(dut.ok("STATE").kv.get("failsafes")))
    rpt.check("fail-safe: relay opens by itself when commands stop", contact_on and dropped and fs >= 1,
              f"wasOn={contact_on} dropped={bool(dropped)} failsafes={fs}")

    jig.ok("MASTEREMU STOP")
    dut.cmd("FACTORYRESET")            # back to default Master/ID so it ships clean
    jig.ok("RESET")


# --------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="WPC factory test (host side)")
    ap.add_argument("dut_type", choices=["master", "pump"])
    ap.add_argument("--jig", help="serial port of the test jig (WM1)")
    ap.add_argument("--dut", help="serial port of the device under test")
    ap.add_argument("--simulate", action="store_true", help="use the built-in simulator instead of hardware")
    ap.add_argument("--sim-fault", default="", help="(with --simulate) inject a fault, see sim_devices.py")
    ap.add_argument("--csv", default="wpc_test_results.csv", help="results log (appended)")
    ap.add_argument("--visual", action="store_true", help="ask the operator to confirm LEDs (pump)")
    ap.add_argument("--wifi-test", action="store_true", help="(master) jig joins the SoftAP and calls /status")
    ap.add_argument("--office-ssid", help="(master) office WiFi for the cloud round-trip check")
    ap.add_argument("--office-pass", default="")
    ap.add_argument("--mqtt-host", default="mqtt.agrisenseandcontrol.in")
    ap.add_argument("--mqtt-user", default="fg1-device")
    ap.add_argument("--mqtt-pass", default="asacfg1")
    # fixture-dependent limits
    ap.add_argument("--min-rssi", type=float, default=-90.0, help="minimum RSSI (dBm) seen at the jig")
    ap.add_argument("--adc-steps", type=lambda s: [int(x) for x in s.split(",")], default=[0, 15, 30, 45],
                    help="PWM duty steps (0-255) for the analog sweep")
    ap.add_argument("--adc-min-span", type=float, default=150.0, help="minimum mV rise across the sweep")
    ap.add_argument("--adc-crosstalk", type=float, default=80.0, help="max mV drift on the untouched channel")
    ap.add_argument("--join-timeout", type=float, default=30.0)
    ap.add_argument("--cmd-timeout", type=float, default=10.0)
    ap.add_argument("--offline-timeout", type=float, default=30.0)
    args = ap.parse_args()

    if args.simulate:
        args.jig = f"sim:jig:{args.dut_type}:{args.sim_fault}"
        args.dut = f"sim:dut:{args.dut_type}:{args.sim_fault}"
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    elif not (args.jig and args.dut):
        ap.error("--jig and --dut are required (or use --simulate)")

    jig = Device("jig", open_port(args.jig))
    dut = Device("dut", open_port(args.dut))
    rpt = Report()
    started = datetime.datetime.now()
    identity = {}
    try:
        jr = jig.wait_ready(15)
        print(f"Jig: fw={jr.kv.get('fw')} mac={jr.kv.get('mac')}")
        identity = dut.wait_ready(30).kv
        (test_master if args.dut_type == "master" else test_pump)(dut, jig, args, rpt)
    except Exception as e:                       # a crash mid-test is a FAIL, never a silent pass
        rpt.check("test sequence completed without error", False, repr(e))

    verdict = "PASS" if rpt.passed else "FAIL"
    print(f"\n=== {args.dut_type.upper()} {identity.get('mac', '?')}: {verdict} "
          f"({sum(1 for r in rpt.rows if r[1])} pass, {sum(1 for r in rpt.rows if r[1] is False)} fail, "
          f"{sum(1 for r in rpt.rows if r[1] is None)} skipped) ===")

    new = not os.path.exists(args.csv)
    with open(args.csv, "a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["timestamp", "dut_type", "mac", "fw", "verdict", "duration_s", "results_json"])
        w.writerow([started.isoformat(timespec="seconds"), args.dut_type, identity.get("mac", ""),
                    identity.get("fw", ""), verdict, round((datetime.datetime.now() - started).total_seconds(), 1),
                    json.dumps([{"test": n, "pass": p, "detail": d} for n, p, d in rpt.rows])])
    return 0 if rpt.passed else 1


if __name__ == "__main__":
    sys.exit(main())
