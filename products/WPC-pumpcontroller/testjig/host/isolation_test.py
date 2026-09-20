"""WPC isolation test: 2 Masters + 2 Pump Nodes on the air at the same time.

Proves that each Master controls ONLY its own Pump: Pump 1 <-> Master A, Pump 2 <-> Master B.
No jig needed -- it uses the boards' own consoles (Pump relay state is read from the Pump's STATE).

    python isolation_test.py --ma <port> --mb <port> --p1 <port> --p2 <port>

Leaves the units PAIRED (Pump1->A, Pump2->B), overrides back to AUTO, test mode off, no factory reset.
"""
import argparse
import sys
import time

import serial

import wpc_test as w
from wpc_station import open_dev

rows = []


def check(name, ok, detail=""):
    rows.append((name, ok))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f"  -- {detail}" if detail else ""), flush=True)
    return ok


def until(fn, timeout, interval=0.5):
    end = time.time() + timeout
    while time.time() < end:
        v = fn()
        if v:
            return v
        time.sleep(interval)
    return None


def relay(pump):
    return pump.ok("STATE").kv.get("relay")


def slot_of(master, pump_id):
    for d in master.ok("PUMPS").data:
        kv = w.parse_kv(d)
        if kv.get("pumpId") == pump_id:
            return kv["slot"]
    return None


def stays(pump, want, secs):
    """True if this Pump's relay is `want` for the whole window (sampled every second)."""
    end = time.time() + secs
    seen = set()
    while time.time() < end:
        seen.add(relay(pump))
        time.sleep(1)
    return seen == {want}, sorted(seen)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ma", required=True)
    ap.add_argument("--mb", required=True)
    ap.add_argument("--p1", required=True)
    ap.add_argument("--p2", required=True)
    ap.add_argument("--pair-timeout", type=float, default=90)
    ap.add_argument("--hold", type=float, default=12, help="seconds to watch the untouched Pump")
    a = ap.parse_args()

    dev = {k: open_dev(k, v) for k, v in (("A", a.ma), ("B", a.mb), ("P1", a.p1), ("P2", a.p2))}
    A, B, P1, P2 = dev["A"], dev["B"], dev["P1"], dev["P2"]
    holds = []
    try:
        info = {k: d.wait_ready(40).kv for k, d in dev.items()}
        ida, idb = info["A"]["masterId"], info["B"]["masterId"]
        id1, id2 = info["P1"]["pumpId"], info["P2"]["pumpId"]
        print(f"Master A {ida} ({info['A']['mac']})   Master B {idb} ({info['B']['mac']})")
        print(f"Pump 1 {id1} ({info['P1']['mac']})   Pump 2 {id2} ({info['P2']['mac']})")
        check("the two Masters have different IDs", ida != idb, f"{ida} vs {idb}")
        for d in dev.values():
            d.ok("TESTMODE 1")        # fast poll / 8 s fail-safe; switched off again at the end

        # ---- 1. pair, with all four on the air at once
        A.ok("FORGETALL"); B.ok("FORGETALL")
        P1.ok(f"MASTER {ida}"); P2.ok(f"MASTER {idb}")
        both = until(lambda: P1.ok("STATE").kv.get("joined") == "1" and P2.ok("STATE").kv.get("joined") == "1", a.pair_timeout)
        check("both Pumps join (each to its own Master) while all four transmit", bool(both))
        sa = until(lambda: slot_of(A, id1), 30)
        sb = until(lambda: slot_of(B, id2), 30)
        check("Master A lists Pump 1 and Master B lists Pump 2", sa is not None and sb is not None, f"slotA={sa} slotB={sb}")
        ta = [w.parse_kv(x).get("pumpId") for x in A.ok("PUMPS").data]
        tb = [w.parse_kv(x).get("pumpId") for x in B.ok("PUMPS").data]
        check("Master A lists ONLY Pump 1, Master B lists ONLY Pump 2 (no cross-pairing)",
              ta == [id1] and tb == [id2], f"A={ta} B={tb}")
        check("Pump 1 points at A, Pump 2 points at B",
              P1.ok("STATE").kv.get("master") == ida and P2.ok("STATE").kv.get("master") == idb)
        if sa is None or sb is None:
            return

        def online(m, pid):
            return any(w.parse_kv(x).get("pumpId") == pid and w.parse_kv(x).get("online") == "1" for x in m.ok("PUMPS").data)
        check("both Pumps show ONLINE at their own Master",
              bool(until(lambda: online(A, id1) and online(B, id2), 40)))

        # ---- 2. command one Pump, the other must not move
        for who, M, slot, target, other, tname, oname in (("A", A, sa, P1, P2, "Pump 1", "Pump 2"),
                                                         ("B", B, sb, P2, P1, "Pump 2", "Pump 1")):
            M.ok(f"OVERRIDE {slot} on")
            on = until(lambda: relay(target) == "1", 25)
            still, seen = stays(other, "0", a.hold)
            check(f"Master {who} ON -> {tname} relay ON, {oname} stays OFF", bool(on) and still,
                  f"{tname} on={bool(on)}, {oname} saw {seen}")
            M.ok(f"OVERRIDE {slot} off")
            off = until(lambda: relay(target) == "0", 25)
            check(f"Master {who} OFF -> {tname} relay OFF", bool(off))
            M.ok(f"OVERRIDE {slot} auto")

        # ---- 3. both at once, and opposite states at once
        A.ok(f"OVERRIDE {sa} on"); B.ok(f"OVERRIDE {sb} on")
        both_on = until(lambda: relay(P1) == "1" and relay(P2) == "1", 30)
        check("both Masters ON at once -> both Pumps ON", bool(both_on))
        A.ok(f"OVERRIDE {sa} off")
        p1off = until(lambda: relay(P1) == "0", 25)
        still, seen = stays(P2, "1", a.hold)
        check("Master A OFF while B is ON -> Pump 1 OFF, Pump 2 stays ON", bool(p1off) and still, f"P2 saw {seen}")

        # ---- 4. fail-safe isolation: Master A dies while both Pumps are ON; only Pump 1 may drop
        A.ok(f"OVERRIDE {sa} on"); B.ok(f"OVERRIDE {sb} on")
        both_on = until(lambda: relay(P1) == "1" and relay(P2) == "1", 30)
        check("before the kill: both Pumps ON", bool(both_on))
        hold = serial.Serial()
        hold.port, hold.baudrate, hold.dtr, hold.rts = a.ma, 115200, False, True
        A.port.close()
        hold.open(); holds.append(hold)             # Master A held in reset = dead
        t0 = time.time()
        seen2 = set()
        p1_dropped = None
        while time.time() - t0 < 25:
            seen2.add(relay(P2))
            if relay(P1) == "0" and p1_dropped is None:
                p1_dropped = time.time() - t0
            if p1_dropped is not None and time.time() - t0 > p1_dropped + 6:
                break
            time.sleep(1)
        check("Master A dead -> Pump 1 fails safe to OFF", p1_dropped is not None,
              f"after {p1_dropped:.1f}s" if p1_dropped is not None else "still ON after 25s")
        check("Master A dead -> Pump 2 (Master B) keeps running the whole time", seen2 == {"1"}, f"P2 saw {sorted(seen2)}")
        hold.rts = False; hold.close(); holds.clear()   # release Master A
        A = open_dev("A", a.ma)
        A.wait_ready(40); A.ok("TESTMODE 1")
        back = until(lambda: P1.ok("STATE").kv.get("joined") == "1" and slot_of(A, id1) is not None, 60)
        check("Master A restarts -> Pump 1 is back under Master A (pairing kept in NVS)", bool(back))
        check("Pump 2 still under Master B after all that", P2.ok("STATE").kv.get("master") == idb and online(B, id2))

        # ---- restore
        for M, s in ((A, sa), (B, sb)):
            try:
                s2 = slot_of(M, id1 if M is A else id2)
                M.ok(f"OVERRIDE {s2} auto")
            except Exception:
                pass
        for d in (A, B, P1, P2):
            d.ok("TESTMODE 0")
    except Exception as e:
        check("test sequence completed without error", False, repr(e))
    finally:
        for h in holds:
            try:
                h.rts = False; h.close()
            except Exception:
                pass
        for d in dev.values():
            try:
                d.port.close()
            except Exception:
                pass
    fails = [n for n, ok in rows if not ok]
    print(f"\n=== ISOLATION: {'PASS' if not fails else 'FAIL'} ({sum(ok for _, ok in rows)} pass, {len(fails)} fail) ===")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
