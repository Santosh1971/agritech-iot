"""Approximate KiCad's zone fill to check the GND pours, for when KiCad isn't at hand.

Each copper layer is rasterised at 0.1 mm. Other nets' pads, tracks and vias (grown by the zone
clearance plus half the minimum pour width), the board edge, the mounting holes and the antenna
keep-out are blocked; the rest is GND pour. Then:
  - every GND pad must touch a pour region that holds a GND via or through-hole pad
    (front SMD pads: front pour; back SMD pads: back pour);
  - all GND vias and through-hole GND pads must be one connected piece of back pour
    (so the two layers are one GND).
KiCad's real fill (press B) and DRC have the last word.

    python3 tools/check_pour.py
"""
import collections, math, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
b = sexpr.parse(open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb")).read())
OX, OY, W, H, R = 100.0, 60.0, D.BOARD_W, D.BOARD_H, D.CORNER_R
STEP, CLR, MINW = 0.1, 0.3, 0.25
GROW = CLR + MINW / 2
NET = "GND"
nx, ny = int(W / STEP) + 1, int(H / STEP) + 1
X, Y = np.meshgrid(np.arange(nx) * STEP, np.arange(ny) * STEP)

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def window(x0, y0, x1, y1):
    i0, i1 = max(int((y0) / STEP), 0), min(int((y1) / STEP) + 2, ny)
    j0, j1 = max(int((x0) / STEP), 0), min(int((x1) / STEP) + 2, nx)
    return slice(i0, i1), slice(j0, j1)

def block_rect(L, cx, cy, w, h, a, grow):
    r = math.hypot(w, h) / 2 + grow
    sl = window(cx - r, cy - r, cx + r, cy + r)
    lx, ly = X[sl] - cx, Y[sl] - cy
    a_ = math.radians(-a)
    u = lx * math.cos(a_) + ly * math.sin(a_); v = -lx * math.sin(a_) + ly * math.cos(a_)
    du, dv = np.maximum(np.abs(u) - w / 2, 0), np.maximum(np.abs(v) - h / 2, 0)
    L[sl] &= ~(np.hypot(du, dv) < grow)

def block_seg(L, x1, y1, x2, y2, rad):
    sl = window(min(x1, x2) - rad, min(y1, y2) - rad, max(x1, x2) + rad, max(y1, y2) + rad)
    px, py = X[sl], Y[sl]
    vx, vy = x2 - x1, y2 - y1; L2 = vx * vx + vy * vy
    t = np.clip(((px - x1) * vx + (py - y1) * vy) / L2, 0, 1) if L2 else 0
    L[sl] &= ~(np.hypot(px - x1 - t * vx, py - y1 - t * vy) < rad)

def label(L):
    lab = np.zeros(L.shape, dtype=np.int32); n = 0
    for i0, j0 in zip(*np.nonzero(L)):
        if lab[i0, j0]:
            continue
        n += 1; lab[i0, j0] = n; dq = collections.deque([(i0, j0)])
        while dq:
            i, j = dq.popleft()
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                a, c = i + di, j + dj
                if 0 <= a < ny and 0 <= c < nx and L[a, c] and not lab[a, c]:
                    lab[a, c] = n; dq.append((a, c))
    return lab

def regions_near(lab, x, y, r):
    sl = window(x - r, y - r, x + r, y + r)
    near = np.hypot(X[sl] - x, Y[sl] - y) <= r
    return set(np.unique(lab[sl][near])) - {0}

class DSU:
    def __init__(self): self.p = {}
    def find(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]; a = self.p[a]
        return a
    def union(self, a, b): self.p[self.find(a)] = self.find(b)

# When check_connect.py finds GND joined by copper (pads, tracks, vias), every GND pad is one piece,
# and this check's job is only to find pour patches and stitching vias that reach nothing (dead copper).
import subprocess
COPPER_JOINED = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "check_connect.py"),
                                "--include-gnd"], capture_output=True).returncode == 0

def analyze(b):
    """Returns per-layer free masks and region labels, the DSU of GND pieces, the main piece,
    the GND pads and vias, and the pieces' membership."""
    nets = {int(n[1]): n[2] for n in find(b, "net")}
    # board area, inset by the pour's edge clearance
    inside = (X >= GROW) & (X <= W - GROW) & (Y >= GROW) & (Y <= H - GROW)
    for cx in (R, W - R):
        corner = (Y < R) & ((X < R) if cx == R else (X > W - R))
        inside &= ~(corner & (np.hypot(X - cx, Y - R) > R - GROW))
    for hx, hy in D.HOLES:
        inside &= np.hypot(X - hx, Y - (H - hy)) > 3.2 / 2 + GROW + 0.5


    layers = {"F.Cu": inside.copy(), "B.Cu": inside.copy()}
    for z in find(b, "zone"):                                 # keep-out rule areas
        if first(z, "keepout") is not None:
            pts = [(float(p[1]) - OX, float(p[2]) - OY) for p in find(first(first(z, "polygon"), "pts"), "xy")]
            x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
            x1, y1 = max(p[0] for p in pts), max(p[1] for p in pts)
            for L in layers.values():
                L &= ~((X >= x0) & (X <= x1) & (Y >= y0) & (Y <= y1))

    gnd_pads, anchors = [], {"F.Cu": [], "B.Cu": []}


    for f in find(b, "footprint"):
        ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
        at = first(f, "at"); fx, fy, fr = float(at[1]) - OX, float(at[2]) - OY, float(at[3]) if len(at) > 3 else 0.0
        for p in find(f, "pad"):
            pa, sz = first(p, "at"), first(p, "size")
            dx, dy = rot(float(pa[1]), float(pa[2]), fr)
            x, y, w, h, a = fx + dx, fy + dy, float(sz[1]), float(sz[2]), float(pa[3]) if len(pa) > 3 else 0.0
            lay = first(p, "layers")[1:]
            on = ["F.Cu", "B.Cu"] if ("*.Cu" in lay or p[2] in ("thru_hole", "np_thru_hole")) else [l for l in lay if l.endswith(".Cu")]
            n = first(p, "net"); net = n[2] if n else ""
            if p[2] == "np_thru_hole":
                for l in ("F.Cu", "B.Cu"):
                    block_rect(layers[l], x, y, w, h, a, GROW)
                continue
            if net == NET:
                gnd_pads.append((ref, p[1], x, y, on, p[2]))
                if p[2] == "thru_hole":
                    for l in on:
                        anchors[l].append((x, y))
                continue
            for l in on:
                block_rect(layers[l], x, y, w, h, a, GROW)
    for t in find(b, "segment"):
        st, en = first(t, "start"), first(t, "end")
        if nets[int(first(t, "net")[1])] != NET:
            block_seg(layers[first(t, "layer")[1]], float(st[1]) - OX, float(st[2]) - OY, float(en[1]) - OX, float(en[2]) - OY,
                      float(first(t, "width")[1]) / 2 + GROW)
    gnd_vias = []
    for v in find(b, "via"):
        at = first(v, "at"); x, y = float(at[1]) - OX, float(at[2]) - OY
        if nets[int(first(v, "net")[1])] == NET:
            gnd_vias.append((x, y))
            for l in ("F.Cu", "B.Cu"):
                anchors[l].append((x, y))
        else:
            for l in ("F.Cu", "B.Cu"):
                block_seg(layers[l], x, y, x, y, float(first(v, "size")[1]) / 2 + GROW)

    # GND tracks are copper of their own: they join whatever pour they touch, and carry pads to vias
    gnd_tracks = {"F.Cu": [], "B.Cu": []}
    for t in find(b, "segment"):
        if nets[int(first(t, "net")[1])] == NET:
            st, en = first(t, "start"), first(t, "end")
            gnd_tracks[first(t, "layer")[1]].append((float(st[1]) - OX, float(st[2]) - OY, float(en[1]) - OX, float(en[2]) - OY))




    labs = {l: label(layers[l]) for l in layers}
    dsu = DSU()
    # vias and through-hole pads join the two layers' regions they sit in
    for l in layers:
        for x, y in anchors[l]:
            dsu.union(("pt", round(x, 2), round(y, 2)), ("pt", round(x, 2), round(y, 2)))
    for x, y in set(anchors["F.Cu"]) | set(anchors["B.Cu"]):
        key = ("pt", round(x, 2), round(y, 2))
        for l in layers:
            for rg in regions_near(labs[l], x, y, 0.6):
                dsu.union(key, (l, int(rg)))
    for l, segs in gnd_tracks.items():
        for (x1, y1, x2, y2) in segs:
            pts = [(x1 + (x2 - x1) * k / 10, y1 + (y2 - y1) * k / 10) for k in range(11)]
            keys = [("pt", round(x, 2), round(y, 2)) for x, y in ((x1, y1), (x2, y2))]
            dsu.union(keys[0], keys[1])
            for (x, y) in pts:
                for rg in regions_near(labs[l], x, y, 0.45):
                    dsu.union(keys[0], (l, int(rg)))
            for x, y in ((x1, y1), (x2, y2)):
                for (ax, ay) in anchors["F.Cu"] + anchors["B.Cu"]:
                    if math.hypot(ax - x, ay - y) < 0.05:
                        dsu.union(keys[0], ("pt", round(ax, 2), round(ay, 2)))

    errors, groups = [], collections.Counter()
    for ref, num, x, y, on, kind in gnd_pads:
        key = ("pad", ref, num, round(x, 2), round(y, 2))
        for l in on:
            for rg in regions_near(labs[l], x, y, 1.0):
                dsu.union(key, (l, int(rg)))
            for (x1, y1, x2, y2) in gnd_tracks[l]:
                for (tx, ty) in ((x1, y1), (x2, y2)):
                    if math.hypot(tx - x, ty - y) < 0.05:
                        dsu.union(key, ("pt", round(x2, 2), round(y2, 2)) if (tx, ty) == (x1, y1) else ("pt", round(x1, 2), round(y1, 2)))
        if kind == "thru_hole":
            dsu.union(key, ("pt", round(x, 2), round(y, 2)))
        if COPPER_JOINED:                             # check_connect.py traced GND copper: the pads are one piece
            dsu.union(key, ("copper", NET))
        groups[dsu.find(key)] += 1
    main = groups.most_common(1)[0][0]

    return dict(layers=layers, labs=labs, dsu=dsu, main=main, gnd_pads=gnd_pads, gnd_vias=gnd_vias, groups=groups, gnd_tracks=gnd_tracks)

def report(a):
    errors = []
    dsu, main = a["dsu"], a["main"]
    for ref, num, x, y, on, kind in a["gnd_pads"]:
        if dsu.find(("pad", ref, num, round(x, 2), round(y, 2))) != main:
            errors.append("%s pad %s is not joined to the main GND" % (ref, num))
    for x, y in a["gnd_vias"]:
        if COPPER_JOINED and any(math.hypot(x - tx, y - ty) < 0.3 for l in a["gnd_tracks"].values()
                                 for (x1, y1, x2, y2) in l for tx, ty in ((x1, y1), (x2, y2))):
            continue                                  # on a GND track: joined by copper
        if dsu.find(("pt", round(x, 2), round(y, 2))) != main:
            errors.append("GND via at (%.1f, %.1f) is isolated" % (x, y))
    return errors

def fix(b, a):
    """Add a GND via inside each piece that isn't the main one, where the other layer is main GND
    and both layers have room for the via (0.3 mm radius + 0.19 mm clearance)."""
    from sexpr import Sym
    import uuid
    nid = [int(n[1]) for n in find(b, "net") if n[2] == NET][0]
    labs, dsu, main = a["labs"], a["dsu"], a["main"]
    need = int(math.ceil((0.3 + 0.19 - GROW) / STEP)) + 1
    ok = {}
    for l, L in a["layers"].items():
        e = L.copy()
        for _ in range(need):
            e = e & np.roll(e, 1, 0) & np.roll(e, -1, 0) & np.roll(e, 1, 1) & np.roll(e, -1, 1)
        ok[l] = e
    both = ok["F.Cu"] & ok["B.Cu"]
    added = 0
    for l, other in (("F.Cu", "B.Cu"), ("B.Cu", "F.Cu")):
        lab, lab_o = labs[l], labs[other]
        for rg in np.unique(lab):
            if rg == 0 or dsu.find((l, int(rg))) == main:
                continue
            cand = both & (lab == rg)
            mains = [r for r in np.unique(lab_o[cand]) if r and dsu.find((other, int(r))) == main]
            if not mains:
                continue
            cand &= np.isin(lab_o, mains)
            ii, jj = np.nonzero(cand)
            if not len(ii):
                continue
            k = len(ii) // 2
            x, y = jj[k] * STEP, ii[k] * STEP
            b.insert(len(b) - 1, [Sym("via"), [Sym("at"), round(OX + x, 4), round(OY + y, 4)], [Sym("size"), 0.6], [Sym("drill"), 0.3],
                                  [Sym("layers"), "F.Cu", "B.Cu"], [Sym("net"), nid], [Sym("uuid"), str(uuid.uuid4())]])
            dsu.union((l, int(rg)), main)
            added += 1
    return added

a = analyze(b)
if "--fix" in sys.argv:
    total = 0
    for _ in range(10):
        n = fix(b, a)
        if not n:
            break
        total += n
        a = analyze(b)
    open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb"), "w").write(sexpr.dump(b) + "\n")
    print("added %d island vias" % total)
errors = report(a)
groups, main = a["groups"], a["main"]
if os.environ.get("POUR_PNG"):                            # picture: main GND bright, other pieces yellow
    import struct, zlib
    rows = []
    for l, base in (("F.Cu", (200, 60, 60)), ("B.Cu", (60, 110, 220))):
        img = np.zeros((ny, nx, 3), dtype=np.uint8) + 25
        lab = a["labs"][l]
        for rg in np.unique(lab):
            if rg == 0:
                continue
            col = base if a["dsu"].find((l, int(rg))) == main else (240, 210, 40)
            img[lab == rg] = col
        rows.append(img)
    img = np.concatenate([rows[0], np.zeros((10, nx, 3), np.uint8) + 255, rows[1]], axis=0)
    raw = b"".join(b"\x00" + img[i].tobytes() for i in range(img.shape[0]))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", img.shape[1], img.shape[0], 8, 2, 0, 0, 0)) + \
          chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(os.environ["POUR_PNG"], "wb").write(png)
print("%d GND pads, %d GND vias, %d separate GND pieces" % (len(a["gnd_pads"]), len(a["gnd_vias"]), len(groups)))
if errors:
    print("\n".join("FAIL: " + e for e in errors)); sys.exit(1)
print("OK: every GND pad and via is one connected GND (approximate fill; confirm with KiCad's fill and DRC)")
