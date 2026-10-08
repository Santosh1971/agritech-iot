"""Finish the few connections Freerouting leaves open: a two-layer A* grid router.

For each net that check_connect.py reports split, it joins the pieces one by one: from any copper
of the piece holding the most pads to any copper of another piece, on a 0.2 mm grid, using F.Cu,
B.Cu and vias. A cell is usable only if a track of the net's width keeps the clearance from every
other net's copper, the board edge, the mounting holes and the antenna keep-out. The new tracks
are written into the board.

    python3 tools/patch_route.py [NET ...]          (default: every split net except GND)
"""
import heapq, math, os, subprocess, sys, uuid
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import Sym, find, first, dump
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
PCB = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
OX, OY, W, H, R = 100.0, 60.0, D.BOARD_W, D.BOARD_H, D.CORNER_R
STEP, CLR, VIA_D, VIA_DRILL = 0.2, 0.2, 0.6, 0.3
WIDTH = {"+5V": 0.45, "/VIN": 0.45, "/VBUS": 0.45, "+3V3": 0.3}
VIA_COST, TURN_COST = 25.0, 0.5

b = sexpr.parse(open(PCB).read())
nets = {int(n[1]): n[2] for n in find(b, "net")}
NET_ID = {v: k for k, v in nets.items()}
nx, ny = int(W / STEP) + 1, int(H / STEP) + 1
X, Y = np.meshgrid(np.arange(nx) * STEP, np.arange(ny) * STEP)

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def window(x0, y0, x1, y1):
    return (slice(max(int(y0 / STEP), 0), min(int(y1 / STEP) + 2, ny)), slice(max(int(x0 / STEP), 0), min(int(x1 / STEP) + 2, nx)))

def rect_dist(sl, cx, cy, w, h, a):
    lx, ly = X[sl] - cx, Y[sl] - cy
    t = math.radians(-a)
    u = lx * math.cos(t) + ly * math.sin(t); v = -lx * math.sin(t) + ly * math.cos(t)
    return np.hypot(np.maximum(np.abs(u) - w / 2, 0), np.maximum(np.abs(v) - h / 2, 0))

def seg_dist(sl, x1, y1, x2, y2):
    px, py = X[sl], Y[sl]
    vx, vy = x2 - x1, y2 - y1; L2 = vx * vx + vy * vy
    t = np.clip(((px - x1) * vx + (py - y1) * vy) / L2, 0, 1) if L2 else 0
    return np.hypot(px - x1 - t * vx, py - y1 - t * vy)

# ---- copper inventory ------------------------------------------------------------------------
pads, segs, vias, holes = [], [], [], []
for f in find(b, "footprint"):
    ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
    at = first(f, "at"); fx, fy, fr = float(at[1]) - OX, float(at[2]) - OY, float(at[3]) if len(at) > 3 else 0.0
    for p in find(f, "pad"):
        pa, sz = first(p, "at"), first(p, "size")
        dx, dy = rot(float(pa[1]), float(pa[2]), fr)
        if p[2] == "np_thru_hole":
            d = first(p, "drill"); holes.append((fx + dx, fy + dy, float(d[2] if d[1] == "oval" else d[1]) / 2)); continue
        lay = first(p, "layers")[1:]
        on = {"F.Cu", "B.Cu"} if (p[2] == "thru_hole" or "*.Cu" in lay) else {l for l in lay if l.endswith(".Cu")}
        n = first(p, "net")
        pads.append(dict(id="%s.%s" % (ref, p[1]), x=fx + dx, y=fy + dy, w=float(sz[1]), h=float(sz[2]),
                         a=float(pa[3]) if len(pa) > 3 else 0.0, layers=on, net=n[2] if n else ""))
for t in find(b, "segment"):
    st, en = first(t, "start"), first(t, "end")
    segs.append(dict(x1=float(st[1]) - OX, y1=float(st[2]) - OY, x2=float(en[1]) - OX, y2=float(en[2]) - OY,
                     hw=float(first(t, "width")[1]) / 2, layer=first(t, "layer")[1], net=nets[int(first(t, "net")[1])]))
for v in find(b, "via"):
    at = first(v, "at")
    vias.append(dict(x=float(at[1]) - OX, y=float(at[2]) - OY, r=float(first(v, "size")[1]) / 2, net=nets[int(first(v, "net")[1])]))
ANT = None
for z in find(b, "zone"):
    if first(z, "keepout") is not None:
        pts = [(float(p[1]) - OX, float(p[2]) - OY) for p in find(first(first(z, "polygon"), "pts"), "xy")]
        ANT = (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))

def free_masks(net, hw):
    """Cells where a track (half-width hw) of `net` keeps clearance, per layer; and where a via fits."""
    base = np.ones((ny, nx), bool)
    edge = hw + 0.3
    base &= (X >= edge) & (X <= W - edge) & (Y >= edge) & (Y <= H - edge)
    for cx in (R, W - R):
        corner = (Y < R) & ((X < R) if cx == R else (X > W - R))
        base &= ~(corner & (np.hypot(X - cx, Y - R) > R - edge))
    for hx, hy in D.HOLES:
        base &= np.hypot(X - hx, Y - (H - hy)) > 3.6
    for (hx, hy, hr) in holes:
        base &= np.hypot(X - hx, Y - hy) > hr + hw + 0.3
    if ANT:
        base &= ~((X >= ANT[0] - hw) & (X <= ANT[2] + hw) & (Y >= ANT[1] - hw) & (Y <= ANT[3] + hw))
    out = {}
    for l in ("F.Cu", "B.Cu"):
        m = base.copy()
        for p in pads:
            if l in p["layers"] and p["net"] != net:
                g = hw + CLR; r = math.hypot(p["w"], p["h"]) / 2 + g
                sl = window(p["x"] - r, p["y"] - r, p["x"] + r, p["y"] + r)
                m[sl] &= rect_dist(sl, p["x"], p["y"], p["w"], p["h"], p["a"]) >= g
        for s in segs:
            if s["layer"] == l and s["net"] != net:
                g = s["hw"] + hw + CLR
                sl = window(min(s["x1"], s["x2"]) - g, min(s["y1"], s["y2"]) - g, max(s["x1"], s["x2"]) + g, max(s["y1"], s["y2"]) + g)
                m[sl] &= seg_dist(sl, s["x1"], s["y1"], s["x2"], s["y2"]) >= g
        for v in vias:
            if v["net"] != net:
                g = v["r"] + hw + CLR
                sl = window(v["x"] - g, v["y"] - g, v["x"] + g, v["y"] + g)
                m[sl] &= np.hypot(X[sl] - v["x"], Y[sl] - v["y"]) >= g
        out[l] = m
    # a via needs its own radius of room on both layers
    vr = VIA_D / 2
    grow = int(math.ceil(max(vr - hw, 0) / STEP))
    vm = out["F.Cu"] & out["B.Cu"]
    for _ in range(grow):
        vm = vm & np.roll(vm, 1, 0) & np.roll(vm, -1, 0) & np.roll(vm, 1, 1) & np.roll(vm, -1, 1)
    return out, vm

def pieces(net):
    """The net's copper split into connected pieces: list of (pad ids, cells per layer)."""
    import collections
    P = [p for p in pads if p["net"] == net]
    S = [s for s in segs if s["net"] == net]
    V = [v for v in vias if v["net"] == net]
    parent = {}
    def fnd(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    def uni(a, c): parent[fnd(a)] = fnd(c)
    def in_pad(px, py, p, slack):
        x, y = rot(px - p["x"], py - p["y"], -p["a"]); return abs(x) <= p["w"] / 2 + slack and abs(y) <= p["h"] / 2 + slack
    def dseg(px, py, s):
        vx, vy = s["x2"] - s["x1"], s["y2"] - s["y1"]; L = vx * vx + vy * vy
        t = 0 if L == 0 else max(0, min(1, ((px - s["x1"]) * vx + (py - s["y1"]) * vy) / L))
        return math.hypot(px - s["x1"] - t * vx, py - s["y1"] - t * vy)
    for i, s in enumerate(S):
        fnd(("s", i))
        for ex, ey in ((s["x1"], s["y1"]), (s["x2"], s["y2"])):
            for k, p in enumerate(P):
                if s["layer"] in p["layers"] and in_pad(ex, ey, p, 0.02):
                    uni(("s", i), ("p", k))
            for j, v in enumerate(V):
                if math.hypot(ex - v["x"], ey - v["y"]) < 0.3:
                    uni(("s", i), ("v", j))
            for j, o in enumerate(S):
                if j != i and o["layer"] == s["layer"] and dseg(ex, ey, o) < max(o["hw"], 0.01):
                    uni(("s", i), ("s", j))
    for j, v in enumerate(V):
        for k, p in enumerate(P):
            if in_pad(v["x"], v["y"], p, 0.3):
                uni(("v", j), ("p", k))
    groups = collections.defaultdict(lambda: dict(pads=[], items=[]))
    for k, p in enumerate(P):
        groups[fnd(("p", k))]["pads"].append(p["id"]); groups[fnd(("p", k))]["items"].append(("p", p))
    for i, s in enumerate(S):
        groups[fnd(("s", i))]["items"].append(("s", s))
    for j, v in enumerate(V):
        groups[fnd(("v", j))]["items"].append(("v", v))
    out = []
    for g in groups.values():
        if not g["pads"]:
            continue
        cells = {"F.Cu": np.zeros((ny, nx), bool), "B.Cu": np.zeros((ny, nx), bool)}
        for kind, it in g["items"]:
            if kind == "p":
                r = math.hypot(it["w"], it["h"]) / 2
                sl = window(it["x"] - r, it["y"] - r, it["x"] + r, it["y"] + r)
                for l in it["layers"]:
                    cells[l][sl] |= rect_dist(sl, it["x"], it["y"], it["w"], it["h"], it["a"]) <= 0.0
            elif kind == "s":
                sl = window(min(it["x1"], it["x2"]) - it["hw"], min(it["y1"], it["y2"]) - it["hw"],
                            max(it["x1"], it["x2"]) + it["hw"], max(it["y1"], it["y2"]) + it["hw"])
                cells[it["layer"]][sl] |= seg_dist(sl, it["x1"], it["y1"], it["x2"], it["y2"]) <= it["hw"]
            else:
                sl = window(it["x"] - it["r"], it["y"] - it["r"], it["x"] + it["r"], it["y"] + it["r"])
                for l in cells:
                    cells[l][sl] |= np.hypot(X[sl] - it["x"], Y[sl] - it["y"]) <= it["r"]
        out.append((g["pads"], cells))
    return sorted(out, key=lambda g: -len(g[0]))

def astar(src, dst, free, viaok):
    """src/dst: per-layer cell masks. Returns a list of (layer, i, j) or None."""
    L = ("F.Cu", "B.Cu")
    di, dj = np.nonzero(dst["F.Cu"] | dst["B.Cu"])
    if not len(di):
        return None
    tgt = np.stack([di, dj], 1)
    def h(i, j):
        d = np.abs(tgt[:, 0] - i) + np.abs(tgt[:, 1] - j)
        return float(d.min())
    openq, came, cost = [], {}, {}
    for li, l in enumerate(L):
        for i, j in zip(*np.nonzero(src[l] & free[l])):
            st = (li, int(i), int(j), -1)
            cost[st] = 0.0; heapq.heappush(openq, (h(i, j), 0.0, st))
    seen = set()
    while openq:
        f, g, st = heapq.heappop(openq)
        li, i, j, d = st
        if (li, i, j) in seen:
            continue
        seen.add((li, i, j))
        if dst[L[li]][i, j]:
            path = [(L[li], i, j)]
            while st in came:
                st = came[st]; path.append((L[st[0]], st[1], st[2]))
            return path[::-1]
        moves = []
        for nd, (a, c) in enumerate(((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1))):
            ii, jj = i + a, j + c
            if 0 <= ii < ny and 0 <= jj < nx and free[L[li]][ii, jj]:
                step = 1.4142 if a and c else 1.0
                moves.append(((li, ii, jj, nd), step + (TURN_COST if d not in (-1, nd) else 0)))
        if viaok[i, j]:
            moves.append(((1 - li, i, j, -1), VIA_COST))
        for nst, c in moves:
            ng = g + c
            if ng < cost.get(nst, 1e18) and (nst[0], nst[1], nst[2]) not in seen:
                cost[nst] = ng; came[nst] = st
                heapq.heappush(openq, (ng + h(nst[1], nst[2]), ng, nst))
    return None

def emit(net, path, width):
    nid = NET_ID[net]
    pts = [(OX + j * STEP, OY + i * STEP, l) for l, i, j in path]
    # merge collinear runs
    out, start = [], 0
    def same_dir(a, b, c):
        return abs((b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])) < 1e-6
    k = 0
    while k < len(pts) - 1:
        a = pts[k]
        if pts[k + 1][2] != a[2]:                       # layer change: a via here
            b.insert(len(b) - 1, [Sym("via"), [Sym("at"), round(a[0], 4), round(a[1], 4)], [Sym("size"), VIA_D], [Sym("drill"), VIA_DRILL],
                                  [Sym("layers"), "F.Cu", "B.Cu"], [Sym("net"), nid], [Sym("uuid"), str(uuid.uuid4())]])
            vias.append(dict(x=a[0] - OX, y=a[1] - OY, r=VIA_D / 2, net=net))
            k += 1; continue
        m = k + 1
        while m + 1 < len(pts) and pts[m + 1][2] == a[2] and same_dir(pts[m - 1], pts[m], pts[m + 1]):
            m += 1
        e = pts[m]
        b.insert(len(b) - 1, [Sym("segment"), [Sym("start"), round(a[0], 4), round(a[1], 4)], [Sym("end"), round(e[0], 4), round(e[1], 4)],
                              [Sym("width"), width], [Sym("layer"), a[2]], [Sym("net"), nid], [Sym("uuid"), str(uuid.uuid4())]])
        segs.append(dict(x1=a[0] - OX, y1=a[1] - OY, x2=e[0] - OX, y2=e[1] - OY, hw=width / 2, layer=a[2], net=net))
        k = m
    return len(path)

todo = sys.argv[1:]
if not todo:
    r = subprocess.run([sys.executable, os.path.join(HERE, "check_connect.py")], capture_output=True, text=True)
    todo = [ln.split()[1].rstrip(":") for ln in r.stdout.splitlines() if ln.startswith("SPLIT ")]
ok = True
for net in todo:
    width = WIDTH.get(net, 0.25)
    for attempt in range(10):
        ps = pieces(net)
        if len(ps) < 2:
            print("%s: joined" % net); break
        free, viaok = free_masks(net, width / 2)
        main_cells = ps[0][1]
        joined = False
        for pads_, cells in ps[1:]:
            path = astar(cells, main_cells, free, viaok)
            if path:
                emit(net, path, width)
                print("%s: joined %s to the main piece (%d cells, %d vias)" % (net, ", ".join(pads_), len(path),
                      sum(1 for a, c in zip(path, path[1:]) if a[0] != c[0])))
                joined = True
                break
            if width > 0.3:                     # try a thinner track before giving up on this piece
                path = astar(cells, main_cells, *free_masks(net, 0.15))
                if path:
                    emit(net, path, 0.3)
                    print("%s: joined %s at 0.3 mm" % (net, ", ".join(pads_)))
                    joined = True
                    break
        if not joined:
            print("%s: no path for %s" % (net, " | ".join(", ".join(p) for p, _ in ps[1:])))
            ok = False
            break
open(PCB, "w").write(dump(b) + "\n")
sys.exit(0 if ok else 1)
