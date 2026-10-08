"""Check that every net is joined by copper (pads, tracks, vias), for when KiCad isn't at hand.

Nets listed in SKIP are joined by the pours instead and are checked by check_pour.py.
Prints each net that is split, with the pads in each piece, and exits 1 if any is.

    python3 tools/check_connect.py [--include-gnd]
"""
import collections, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
b = sexpr.parse(open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb")).read())
nets = {int(n[1]): n[2] for n in find(b, "net")}
SKIP = set() if "--include-gnd" in sys.argv else {"GND"}
EPS = 0.01

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def in_pad(px, py, p, slack=0.0):
    x, y = rot(px - p["x"], py - p["y"], -p["a"])
    return abs(x) <= p["w"] / 2 + slack and abs(y) <= p["h"] / 2 + slack

def d_seg(px, py, s):
    x1, y1, x2, y2 = s["x1"], s["y1"], s["x2"], s["y2"]
    vx, vy = x2 - x1, y2 - y1; L = vx * vx + vy * vy
    t = 0 if L == 0 else max(0, min(1, ((px - x1) * vx + (py - y1) * vy) / L))
    return math.hypot(px - x1 - t * vx, py - y1 - t * vy)

pads, segs, vias = collections.defaultdict(list), collections.defaultdict(list), collections.defaultdict(list)
for f in find(b, "footprint"):
    ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
    at = first(f, "at"); fx, fy, fr = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
    for p in find(f, "pad"):
        n = first(p, "net")
        if not n or n[2] in SKIP or n[2].startswith("unconnected-"):
            continue
        pa, sz = first(p, "at"), first(p, "size")
        dx, dy = rot(float(pa[1]), float(pa[2]), fr)
        lay = first(p, "layers")[1:]
        on = {"F.Cu", "B.Cu"} if (p[2] == "thru_hole" or "*.Cu" in lay) else {l for l in lay if l.endswith(".Cu")}
        pads[n[2]].append(dict(id="%s.%s" % (ref, p[1]), x=fx + dx, y=fy + dy, w=float(sz[1]), h=float(sz[2]),
                               a=float(pa[3]) if len(pa) > 3 else 0.0, layers=on))
for t in find(b, "segment"):
    n = nets[int(first(t, "net")[1])]
    st, en = first(t, "start"), first(t, "end")
    segs[n].append(dict(x1=float(st[1]), y1=float(st[2]), x2=float(en[1]), y2=float(en[2]), layer=first(t, "layer")[1],
                        hw=float(first(t, "width")[1]) / 2))
for v in find(b, "via"):
    n = nets[int(first(v, "net")[1])]
    at = first(v, "at"); vias[n].append((float(at[1]), float(at[2])))

class DSU:
    def __init__(self): self.p = {}
    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]; a = self.p[a]
        return a
    def union(self, a, c): self.p[self.find(a)] = self.find(c)

split = {}
for n, plist in pads.items():
    if len(plist) < 2:
        continue
    dsu = DSU()
    S, V = segs.get(n, []), vias.get(n, [])
    for i, s in enumerate(S):
        dsu.find(("s", i))
        for end in ((s["x1"], s["y1"]), (s["x2"], s["y2"])):
            for k, p in enumerate(plist):
                if s["layer"] in p["layers"] and in_pad(end[0], end[1], p, 0.02):
                    dsu.union(("s", i), ("p", k))
            for j, (vx, vy) in enumerate(V):
                if math.hypot(end[0] - vx, end[1] - vy) < 0.3:
                    dsu.union(("s", i), ("v", j))
            for j, o in enumerate(S):
                if j != i and o["layer"] == s["layer"] and d_seg(end[0], end[1], o) < max(o["hw"], EPS):
                    dsu.union(("s", i), ("s", j))
    for j, (vx, vy) in enumerate(V):
        for k, p in enumerate(plist):
            if in_pad(vx, vy, p, 0.3):
                dsu.union(("v", j), ("p", k))
        for i, s in enumerate(S):                   # a via anywhere along a track joins it
            if d_seg(vx, vy, s) < s["hw"] + 0.05:
                dsu.union(("v", j), ("s", i))
    # pads of one footprint pin that overlap (e.g. a module's centre pads) are one piece
    for k, p in enumerate(plist):
        for m, q in enumerate(plist[k + 1:], k + 1):
            if p["layers"] & q["layers"] and in_pad(q["x"], q["y"], p, max(q["w"], q["h"]) / 2):
                dsu.union(("p", k), ("p", m))
    groups = collections.defaultdict(list)
    for k, p in enumerate(plist):
        groups[dsu.find(("p", k))].append(p["id"])
    if len(groups) > 1:
        split[n] = sorted(groups.values(), key=len, reverse=True)

print("%d nets checked" % len([n for n in pads if len(pads[n]) > 1]))
for n, gs in sorted(split.items()):
    print("SPLIT %s: %s" % (n, " | ".join(", ".join(g) for g in gs)))
if split:
    sys.exit(1)
print("OK: every net is joined by copper%s" % ("" if SKIP == set() else " (GND left to the pours)"))
