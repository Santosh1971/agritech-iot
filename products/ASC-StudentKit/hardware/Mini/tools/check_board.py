"""Checks on the generated board, for when KiCad's DRC isn't at hand:
every pad (copper) at least 0.3 mm inside the outline (USB-C J2 overhangs on purpose),
no pad within 3.0 mm of a mounting-hole centre (screw head), no two footprints' courtyards
touching (gen_pcb.py enforces this while placing), every net with pads on the board.

    python3 tools/check_board.py
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
b = sexpr.parse(open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb")).read())
OX, OY = 100.0, 60.0
W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
HOLES = [(x, H - y) for x, y in D.HOLES]

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def edge_dist(x, y):
    d = min(x, W - x, H - y, y)
    for cx in (R, W - R):
        if y < R and ((cx == R and x < R) or (cx == W - R and x > W - R)):
            d = min(d, R - math.hypot(x - cx, y - R))
    return d

errors, nets = [], {}
for f in find(b, "footprint"):
    ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
    at = first(f, "at"); fx, fy, fr = float(at[1]) - OX, float(at[2]) - OY, float(at[3])
    for p in find(f, "pad"):
        pa, sz = first(p, "at"), first(p, "size")
        dx, dy = rot(float(pa[1]), float(pa[2]), fr)
        x, y = fx + dx, fy + dy
        r = math.hypot(float(sz[1]), float(sz[2])) / 2
        n = first(p, "net")
        if n:
            nets.setdefault(n[2], set()).add(ref)
        if ref != "J2" and p[2] != "np_thru_hole" and edge_dist(x, y) < r + 0.3:
            errors.append("%s pad %s is closer than 0.3 mm to the board edge" % (ref, p[1]))
        if not ref.startswith("H"):
            for hx, hy in HOLES:
                if math.hypot(x - hx, y - hy) < 3.0 + r:
                    errors.append("%s pad %s is under the screw head at (%.1f, %.1f)" % (ref, p[1], hx, hy))
want = {n for p in json.load(open(os.path.join(PRJ, "tools", "netlist.json"))) for n in p["pads"].values()}
for n in sorted(want):
    if len(nets.get(n, ())) < 2:
        errors.append("net %s reaches %s" % (n, ", ".join(sorted(nets.get(n, ()))) or "nothing"))
print("%d footprints, %d nets" % (len(find(b, "footprint")), len(nets)))
if errors:
    print("\n".join("FAIL: " + e for e in errors)); sys.exit(1)
print("OK: pads clear of the edge and the screw heads, every net on the board")
