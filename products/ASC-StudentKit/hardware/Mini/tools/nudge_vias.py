"""Move vias that sit too close to another net's copper, by up to 0.25 mm.

For each via closer than CLEARANCE (default 0.15 mm) to another net's track, via or pad, try small
moves in 16 directions; the ends of its own net's tracks that meet the via move with it, and the
move is kept only if the via and those tracks then clear everything. Writes the board in place.

    CLEARANCE=0.15 python3 tools/nudge_vias.py
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first, dump
import check_clearance as C

CLR = C.CLR
b = C.b
OX, OY = C.OX, C.OY

def others(net, layers):
    return [it for it in C.items if it["net"] != net and it["layers"] & layers]

def clear(cap, obstacles):
    return all(C.dist(cap, o) >= CLR - 1e-3 for o in obstacles)

fixed = left = 0
for v in find(b, "via"):
    at = first(v, "at"); x, y = float(at[1]) - OX, float(at[2]) - OY
    r = float(first(v, "size")[1]) / 2
    net = C.nets[int(first(v, "net")[1])]
    obs = [o for o in others(net, {"F.Cu", "B.Cu"}) if not (o["kind"] == "cap" and o["owner"] == "via" and o["x1"] == x and o["y1"] == y)]
    me = dict(kind="cap", x1=x, y1=y, x2=x, y2=y, r=r, layers={"F.Cu", "B.Cu"}, net=net, owner="via", name="via")
    if clear(me, obs):
        continue
    # this net's tracks that end on the via
    ends = []
    for t in find(b, "segment"):
        if C.nets[int(first(t, "net")[1])] != net:
            continue
        for key in ("start", "end"):
            e = first(t, key)
            if math.hypot(float(e[1]) - OX - x, float(e[2]) - OY - y) < 0.01:
                ends.append((t, key))
    done = False
    for d in (0.05, 0.1, 0.15, 0.2, 0.25):
        for k in range(16):
            nx_, ny_ = x + d * math.cos(k * math.pi / 8), y + d * math.sin(k * math.pi / 8)
            cand = dict(me, x1=nx_, y1=ny_, x2=nx_, y2=ny_)
            if not clear(cand, obs):
                continue
            ok = True
            for t, key in ends:
                other = first(t, "end" if key == "start" else "start")
                lay = first(t, "layer")[1]
                seg = dict(kind="cap", x1=nx_, y1=ny_, x2=float(other[1]) - OX, y2=float(other[2]) - OY,
                           r=float(first(t, "width")[1]) / 2, layers={lay}, net=net, owner="track", name="track")
                if not clear(seg, others(net, {lay})):
                    ok = False; break
            if not ok:
                continue
            at[1], at[2] = round(nx_ + OX, 4), round(ny_ + OY, 4)
            for t, key in ends:
                e = first(t, key); e[1], e[2] = round(nx_ + OX, 4), round(ny_ + OY, 4)
            done = True
            break
        if done:
            break
    fixed += done; left += not done
    print("via (%s) at (%.2f, %.2f): %s" % (net, x, y, "moved" if done else "no room to move"))
open(os.path.join(C.PRJ, C.D.PROJECT + ".kicad_pcb"), "w").write(dump(b) + "\n")
print("%d vias moved, %d still too close" % (fixed, left))
sys.exit(1 if left else 0)
