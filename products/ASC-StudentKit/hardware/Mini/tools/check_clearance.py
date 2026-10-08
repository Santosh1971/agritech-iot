"""Copper clearance check (a DRC subset), for when KiCad isn't at hand.

Tracks, vias and pads of different nets on the same layer must be at least CLR apart; tracks and
vias must stay 0.3 mm inside the board edge and clear of the mounting holes. Pads of the same
footprint are not checked against each other (the footprint's own spacing).

    python3 tools/check_clearance.py
"""
import collections, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
b = sexpr.parse(open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb")).read())
nets = {int(n[1]): n[2] for n in find(b, "net")}
OX, OY, W, H, R = 100.0, 60.0, D.BOARD_W, D.BOARD_H, D.CORNER_R
CLR = float(os.environ.get("CLEARANCE", "0.18"))

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

# every copper item as a capsule (segment + radius) or a rotated rectangle
items = []   # dict(kind, layers, net, owner, geom...)
for f in find(b, "footprint"):
    ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
    at = first(f, "at"); fx, fy, fr = float(at[1]) - OX, float(at[2]) - OY, float(at[3]) if len(at) > 3 else 0.0
    for p in find(f, "pad"):
        if p[2] == "np_thru_hole":
            continue
        pa, sz = first(p, "at"), first(p, "size")
        dx, dy = rot(float(pa[1]), float(pa[2]), fr)
        lay = first(p, "layers")[1:]
        on = {"F.Cu", "B.Cu"} if (p[2] == "thru_hole" or "*.Cu" in lay) else {l for l in lay if l.endswith(".Cu")}
        n = first(p, "net")
        w, h, a = float(sz[1]), float(sz[2]), float(pa[3]) if len(pa) > 3 else 0.0
        if p[3] in ("circle", "oval"):         # as a capsule along the long side
            r = min(w, h) / 2; ln = (max(w, h) - min(w, h)) / 2
            ux, uy = rot(ln if w >= h else 0, 0 if w >= h else ln, a)
            items.append(dict(kind="cap", x1=fx + dx - ux, y1=fy + dy - uy, x2=fx + dx + ux, y2=fy + dy + uy, r=r,
                              layers=on, net=n[2] if n else "", owner=ref, name="%s.%s" % (ref, p[1])))
        else:
            items.append(dict(kind="rect", x=fx + dx, y=fy + dy, w=w, h=h, a=a, layers=on, net=n[2] if n else "", owner=ref,
                              name="%s.%s" % (ref, p[1])))
for t in find(b, "segment"):
    st, en = first(t, "start"), first(t, "end")
    items.append(dict(kind="cap", x1=float(st[1]) - OX, y1=float(st[2]) - OY, x2=float(en[1]) - OX, y2=float(en[2]) - OY,
                      r=float(first(t, "width")[1]) / 2, layers={first(t, "layer")[1]}, net=nets[int(first(t, "net")[1])],
                      owner="track", name="track"))
for v in find(b, "via"):
    at = first(v, "at"); x, y = float(at[1]) - OX, float(at[2]) - OY
    items.append(dict(kind="cap", x1=x, y1=y, x2=x, y2=y, r=float(first(v, "size")[1]) / 2, layers={"F.Cu", "B.Cu"},
                      net=nets[int(first(v, "net")[1])], owner="via", name="via"))

def seg_seg(a1, a2, b1, b2):
    def dps(p, s1, s2):
        vx, vy = s2[0] - s1[0], s2[1] - s1[1]; L = vx * vx + vy * vy
        t = 0 if L == 0 else max(0, min(1, ((p[0] - s1[0]) * vx + (p[1] - s1[1]) * vy) / L))
        return math.hypot(p[0] - s1[0] - t * vx, p[1] - s1[1] - t * vy)
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    if (cross(a1, a2, b1) * cross(a1, a2, b2) < 0) and (cross(b1, b2, a1) * cross(b1, b2, a2) < 0):
        return 0.0
    return min(dps(a1, b1, b2), dps(a2, b1, b2), dps(b1, a1, a2), dps(b2, a1, a2))

def rect_edges(it):
    hw, hh = it["w"] / 2, it["h"] / 2
    cs = [rot(sx * hw, sy * hh, it["a"]) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    cs = [(it["x"] + c[0], it["y"] + c[1]) for c in cs]
    return [(cs[i], cs[(i + 1) % 4]) for i in range(4)]

def in_rect(p, it):
    x, y = rot(p[0] - it["x"], p[1] - it["y"], -it["a"])
    return abs(x) <= it["w"] / 2 and abs(y) <= it["h"] / 2

def dist(a, c):
    if a["kind"] == "cap" and c["kind"] == "cap":
        return seg_seg((a["x1"], a["y1"]), (a["x2"], a["y2"]), (c["x1"], c["y1"]), (c["x2"], c["y2"])) - a["r"] - c["r"]
    if a["kind"] == "rect" and c["kind"] == "cap":
        a, c = c, a
    if a["kind"] == "cap":                        # capsule to rectangle
        if in_rect((a["x1"], a["y1"]), c) or in_rect((a["x2"], a["y2"]), c):
            return -a["r"]
        return min(seg_seg((a["x1"], a["y1"]), (a["x2"], a["y2"]), e1, e2) for e1, e2 in rect_edges(c)) - a["r"]
    ea, ec = rect_edges(a), rect_edges(c)
    if any(in_rect(e[0], c) for e in ea) or any(in_rect(e[0], a) for e in ec):
        return 0.0
    return min(seg_seg(p1, p2, q1, q2) for p1, p2 in ea for q1, q2 in ec)

def bbox(it):
    if it["kind"] == "cap":
        return (min(it["x1"], it["x2"]) - it["r"], min(it["y1"], it["y2"]) - it["r"], max(it["x1"], it["x2"]) + it["r"], max(it["y1"], it["y2"]) + it["r"])
    r = math.hypot(it["w"], it["h"]) / 2
    return (it["x"] - r, it["y"] - r, it["x"] + r, it["y"] + r)

grid = collections.defaultdict(list)
G = 2.0
for k, it in enumerate(items):
    x0, y0, x1, y1 = bbox(it)
    for gi in range(int((x0 - CLR) // G), int((x1 + CLR) // G) + 1):
        for gj in range(int((y0 - CLR) // G), int((y1 + CLR) // G) + 1):
            grid[(gi, gj)].append(k)
errors, seen = [], set()
for cell in grid.values():
    for i in range(len(cell)):
        for j in range(i + 1, len(cell)):
            a, c = items[cell[i]], items[cell[j]]
            key = (min(cell[i], cell[j]), max(cell[i], cell[j]))
            if key in seen:
                continue
            seen.add(key)
            if not (a["layers"] & c["layers"]) or (a["net"] and a["net"] == c["net"]):
                continue
            if a["owner"] == c["owner"] and a["owner"] not in ("track", "via"):
                continue
            d = dist(a, c)
            if d < CLR - 1e-3:
                errors.append((round(d, 3), a["name"], a["net"], c["name"], c["net"], sorted(a["layers"] & c["layers"])))
# edge and holes for tracks and vias
def edge_dist(x, y):
    d = min(x, W - x, H - y, y)
    for cx in (R, W - R):
        if y < R and ((cx == R and x < R) or (cx == W - R and x > W - R)):
            d = min(d, R - math.hypot(x - cx, y - R))
    return d
for it in items:
    if it["owner"] in ("track", "via"):
        for x, y in ((it["x1"], it["y1"]), (it["x2"], it["y2"]), ((it["x1"] + it["x2"]) / 2, (it["y1"] + it["y2"]) / 2)):
            if edge_dist(x, y) < it["r"] + 0.3:
                errors.append((round(edge_dist(x, y) - it["r"], 3), it["name"], it["net"], "board edge", "", []))
            for hx, hy in D.HOLES:
                if math.hypot(x - hx, y - (H - hy)) < 1.6 + it["r"] + 0.3:
                    errors.append((0, it["name"], it["net"], "mounting hole", "", []))
print("%d copper items checked at %.2f mm" % (len(items), CLR))
for e in sorted(errors)[:40]:
    print("FAIL %.3f mm: %s (%s) <-> %s (%s) %s" % e)
if errors:
    print("%d clearance problems" % len(errors)); sys.exit(1)
print("OK: no clearance problems")
