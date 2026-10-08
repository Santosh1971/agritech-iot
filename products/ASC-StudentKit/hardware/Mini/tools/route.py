"""Route ASC-Mini.kicad_pcb with Freerouting, without KiCad.

  1. export route/ASC-Mini.dsn (Specctra) from the board gen_pcb.py wrote
  2. run Freerouting headless (FREEROUTING_JAR, e.g. freerouting-2.5.0-executable.jar from Maven Central)
  3. import route/ASC-Mini.ses back into the board as tracks and vias

    FREEROUTING_JAR=/path/freerouting-2.5.0-executable.jar python3 tools/route.py

Freerouting 2.x needs Java 25 or later; set JAVA to its java binary if the default is older.

The zones stay unfilled: open the board in KiCad, press B, then run DRC.
"""
import math, os, re, subprocess, sys, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import Sym, find, first, dump
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
PCB = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
OUT = os.path.join(PRJ, "route")
os.makedirs(OUT, exist_ok=True)
DSN, SES = os.path.join(OUT, D.PROJECT + ".dsn"), os.path.join(OUT, D.PROJECT + ".ses")

# Track widths (mm) per net class; clearance 0.2 mm everywhere (JLC's minimum is 0.127).
CLEARANCE = 0.19                  # the USB-C footprint's own pad gaps are exactly 0.2 mm
VIA = (0.6, 0.3)
CLASSES = {
    "power": (0.6, {"VIN_RAW", "VIN", "/VIN_RAW", "/VIN", "/+5V_BUCK", "/BUCK_SW", "+5V", "/VBUS", "/VBUS_F"}),
    "supply": (0.4, {"+3V3", "GND", "/+5V_PORT", "/+3V3_PORT"}),
    "contacts": (1.0, {"/OUT1_COM", "/OUT1_NO", "/OUT2_COM", "/OUT2_NO"}),
}
DEFAULT_W = 0.25
# Nets left to the copper pours: not autorouted; each front SMD pad gets a short track to its own
# via into the back pour, and stitching vias tie the two pours together (see stitch()).
POUR_NETS = {"GND"}

def uid():
    return str(uuid.uuid4())

board = sexpr.parse(open(PCB).read())
nets = {int(n[1]): n[2] for n in find(board, "net")}
OX, OY = 100.0, 60.0

def um(v):
    return int(round(v * 1000))

def q(s):
    return '"%s"' % s

# ---- export DSN ------------------------------------------------------------------------------
def outline_pts():
    W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
    pts = []
    for cx, a0, a1 in ((R, 180, 270), (W - R, 270, 360)):
        for i in range(17):
            a = math.radians(a0 + (a1 - a0) * i / 16)
            pts.append((cx + R * math.cos(a), R + R * math.sin(a)))
    pts += [(W, H), (0, H)]
    pts.append(pts[0])
    return pts

def xy(x, y):                       # board mm (y down) -> DSN um (y up), page origin
    return "%d %d" % (um(OX + x), -um(OY + y))

padstacks = {}
def padstack(pad, smd_layer):
    kind, shape = pad[2], pad[3]
    sz = first(pad, "size"); w, h = float(sz[1]), float(sz[2])
    layers = ["F.Cu", "B.Cu"] if kind == "thru_hole" else [smd_layer]
    if shape == "circle":
        name = "Round_%d_%s" % (um(w), "".join(l[0] for l in layers))
        shapes = ["(shape (circle %s %d))" % (l, um(w)) for l in layers]
    elif shape == "oval":
        name = "Oval_%dx%d_%s" % (um(w), um(h), "".join(l[0] for l in layers))
        if w >= h:
            seg = "%d 0 %d 0" % (-um((w - h) / 2), um((w - h) / 2)); width = um(h)
        else:
            seg = "0 %d 0 %d" % (-um((h - w) / 2), um((h - w) / 2)); width = um(w)
        shapes = ["(shape (path %s %d %s))" % (l, width, seg) for l in layers]
    else:                            # rect, roundrect, custom: the bounding rectangle
        name = "Rect_%dx%d_%s" % (um(w), um(h), "".join(l[0] for l in layers))
        shapes = ["(shape (rect %s %d %d %d %d))" % (l, -um(w / 2), -um(h / 2), um(w / 2), um(h / 2)) for l in layers]
    padstacks[name] = "(padstack %s %s (attach off))" % (q(name), " ".join(shapes))
    return name

images, places, pin_nets = {}, [], {}
for f in find(board, "footprint"):
    ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
    at = first(f, "at"); fx, fy = float(at[1]) - OX, float(at[2]) - OY
    frot = float(at[3]) if len(at) > 3 else 0.0
    back = first(f, "layer")[1] == "B.Cu"
    img = "%s_%s" % (f[1], ref)          # one image per part keeps back-side flips simple
    pins, keepouts, seen = [], [], {}
    for p in find(f, "pad"):
        pa = first(p, "at")
        lx, ly = float(pa[1]), float(pa[2])
        if back:
            ly = -ly                     # the image is the front view; Specctra flips it for the back
        if p[2] == "np_thru_hole" or p[1] == "":
            d = float(first(p, "drill")[1]) if first(p, "drill")[1] != "oval" else float(first(p, "drill")[2])
            keepouts.append("(keepout \"\" (circle signal %d %d %d))" % (um(d + 0.5), um(lx), -um(ly)))
            continue
        ps = padstack(p, "F.Cu")
        num = p[1]
        n = seen.get(num, 0); seen[num] = n + 1
        pid = num if n == 0 else "%s@%d" % (num, n)
        a = (float(pa[3]) if len(pa) > 3 else 0.0) - frot
        rot = "" if abs(a % 360) < 0.01 else "(rotate %g) " % (a % 360)
        pins.append("(pin %s %s%s %d %d)" % (q(ps), rot, q(pid), um(lx), -um(ly)))
        net = first(p, "net")
        if net and not net[2].startswith("unconnected-"):
            pin_nets.setdefault(net[2], []).append("%s-%s" % (ref, pid))
    images[img] = "(image %s %s %s)" % (q(img), " ".join(pins), " ".join(keepouts))
    places.append("(component %s (place %s %s %s %g))" % (q(img), q(ref), xy(fx, fy), "back" if back else "front", frot % 360))

def klass(n):
    for name, (w, members) in CLASSES.items():
        if n in members:
            return name, w
    return "default", DEFAULT_W

antenna = None
for z in find(board, "zone"):
    if first(z, "keepout") is not None:
        pts = [(float(p[1]) - OX, float(p[2]) - OY) for p in find(first(first(z, "polygon"), "pts"), "xy")]
        antenna = "(keepout \"antenna\" (polygon signal 0 %s))" % " ".join(xy(*p) for p in pts + [pts[0]])

lines = ["(pcb %s" % q(D.PROJECT),
         "(parser (string_quote \") (space_in_quoted_tokens on) (host_cad \"KiCad's Pcbnew\") (host_version \"9.0\"))",
         "(resolution um 10) (unit um)",
         "(structure",
         "(layer F.Cu (type signal) (property (index 0)))",
         "(layer B.Cu (type signal) (property (index 1)))",
         "(boundary (path pcb 0 %s))" % " ".join(xy(*p) for p in outline_pts()),
         antenna or "",
         "(via \"Via_%d_%d\")" % (um(VIA[0]), um(VIA[1])),
         "(rule (width %d) (clearance %d) (clearance %d (type default_smd)) (clearance 50 (type smd_smd)))" % (um(DEFAULT_W), um(CLEARANCE), um(CLEARANCE)),
         ")",
         "(placement", *places, ")",
         "(library", *images.values(), *padstacks.values(),
         "(padstack \"Via_%d_%d\" (shape (circle F.Cu %d)) (shape (circle B.Cu %d)) (attach off))" % (um(VIA[0]), um(VIA[1]), um(VIA[0]), um(VIA[0])),
         ")",
         "(network"]
for n, pins in sorted(pin_nets.items()):
    if len(pins) > 1 and n not in POUR_NETS:
        lines.append("(net %s (pins %s))" % (q(n), " ".join(pins)))
by_class = {}
for n in pin_nets:
    if n not in POUR_NETS:
        by_class.setdefault(klass(n), []).append(n)
for (name, w), members in by_class.items():
    lines.append("(class %s %s (circuit (use_via \"Via_%d_%d\")) (rule (width %d) (clearance %d)))"
                 % (q(name), " ".join(q(m) for m in sorted(members)), um(VIA[0]), um(VIA[1]), um(w), um(CLEARANCE)))
lines += [")", "(wiring)", ")"]
open(DSN, "w").write("\n".join(l for l in lines if l) + "\n")
print("DSN: %d components, %d nets -> %s" % (len(places), len([p for p in pin_nets.values() if len(p) > 1]), DSN))

# ---- run Freerouting ---------------------------------------------------------------------------
jar = os.environ.get("FREEROUTING_JAR")
if jar:
    if os.path.exists(SES):
        os.remove(SES)
    cmd = [os.environ.get("JAVA", "java"), "-jar", jar, "-de", DSN, "-do", SES, "-mp", os.environ.get("ROUTE_PASSES", "30"), "--gui.enabled=false"]
    print(" ".join(cmd))
    subprocess.run(cmd, check=False, cwd=OUT)
if not os.path.exists(SES):
    sys.exit("No session file yet: route %s in Freerouting and save %s, then run this again." % (DSN, SES))

# ---- import the session ---------------------------------------------------------------------------
ses = sexpr.parse(open(SES).read())
res = first(first(ses, "routes"), "resolution")
scale = 1000.0 * float(res[2]) if res[1] == "um" else 1.0   # SES units per mm
NET_ID = {v: k for k, v in nets.items()}
board = [e for e in board if not (isinstance(e, list) and e[0] in ("segment", "via"))]
tail = board.pop()                                   # (embedded_fonts no)
n_seg = n_via = 0
for net in find(first(first(ses, "routes"), "network_out"), "net"):
    name = net[1]
    nid = NET_ID.get(name)
    if nid is None:
        continue
    for w in find(net, "wire"):
        path = first(w, "path")
        layer, width = path[1], float(path[2]) / scale
        c = [float(v) / scale for v in path[3:]]
        pts = [(c[i] - 0, -c[i + 1]) for i in range(0, len(c) - 1, 2)]
        for a, b in zip(pts, pts[1:]):
            board.append([Sym("segment"), [Sym("start"), round(a[0], 4), round(a[1], 4)], [Sym("end"), round(b[0], 4), round(b[1], 4)],
                          [Sym("width"), round(width, 4)], [Sym("layer"), layer], [Sym("net"), nid], [Sym("uuid"), uid()]])
            n_seg += 1
    for v in find(net, "via"):
        x, y = float(v[2]) / scale, -float(v[3]) / scale
        board.append([Sym("via"), [Sym("at"), round(x, 4), round(y, 4)], [Sym("size"), VIA[0]], [Sym("drill"), VIA[1]],
                      [Sym("layers"), "F.Cu", "B.Cu"], [Sym("net"), nid], [Sym("uuid"), uid()]])
        n_via += 1
print("imported %d track segments and %d vias" % (n_seg, n_via))

# ---- pour nets: a via for every front SMD pad, then stitching ----------------------------------------
def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

class Copper:
    """Every piece of copper on the board, for clearance checks: pads as rotated rectangles,
    tracks as capsules, vias as circles."""
    def __init__(self):
        self.pads, self.tracks, self.vias, self.holes = [], [], [], []

    def add_board(self, items):
        for f in find(items, "footprint"):
            at = first(f, "at"); fx, fy, fr = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
            for p in find(f, "pad"):
                pa, sz = first(p, "at"), first(p, "size")
                dx, dy = rot(float(pa[1]), float(pa[2]), fr)
                ang = float(pa[3]) if len(pa) > 3 else 0.0
                lay = set(first(p, "layers")[1:])
                layers = {"F.Cu", "B.Cu"} if "*.Cu" in lay else {l for l in lay if l.endswith(".Cu")}
                net = first(p, "net")
                if p[2] == "np_thru_hole":
                    d = first(p, "drill"); self.holes.append((fx + dx, fy + dy, float(d[2] if d[1] == "oval" else d[1]) / 2))
                    continue
                self.pads.append(dict(x=fx + dx, y=fy + dy, w=float(sz[1]), h=float(sz[2]), a=ang, layers=layers,
                                      net=net[2] if net else "", smd=p[2] == "smd", ref=[q[2] for q in find(f, "property") if q[1] == "Reference"][0]))
        for t in find(items, "segment"):
            st, en = first(t, "start"), first(t, "end")
            self.tracks.append((float(st[1]), float(st[2]), float(en[1]), float(en[2]), float(first(t, "width")[1]) / 2,
                                first(t, "layer")[1], nets[int(first(t, "net")[1])]))
        for v in find(items, "via"):
            at = first(v, "at"); self.vias.append((float(at[1]), float(at[2]), float(first(v, "size")[1]) / 2, nets[int(first(v, "net")[1])]))

    @staticmethod
    def d_rect(px, py, pad):
        # distance from a point to a rotated rectangle (0 inside)
        x, y = rot(px - pad["x"], py - pad["y"], -pad["a"])
        dx, dy = max(abs(x) - pad["w"] / 2, 0), max(abs(y) - pad["h"] / 2, 0)
        return math.hypot(dx, dy)

    @staticmethod
    def d_seg(px, py, x1, y1, x2, y2):
        vx, vy = x2 - x1, y2 - y1
        L = vx * vx + vy * vy
        t = 0 if L == 0 else max(0, min(1, ((px - x1) * vx + (py - y1) * vy) / L))
        return math.hypot(px - x1 - t * vx, py - y1 - t * vy)

    def clear_point(self, x, y, r, net, layers, gap):
        for p in self.pads:
            if p["layers"] & layers and p["net"] != net and self.d_rect(x, y, p) < r + gap:
                return False
        for (x1, y1, x2, y2, hw, lay, n) in self.tracks:
            if lay in layers and n != net and self.d_seg(x, y, x1, y1, x2, y2) < r + hw + gap:
                return False
        for (vx, vy, vr, n) in self.vias:
            if math.hypot(x - vx, y - vy) < r + vr + (gap if n != net else 0.2):
                return False
        for (hx, hy, hr) in self.holes:
            if math.hypot(x - hx, y - hy) < r + hr + 0.3:
                return False
        return True

    def clear_track(self, x1, y1, x2, y2, hw, net, layer, gap):
        n = max(2, int(math.hypot(x2 - x1, y2 - y1) / 0.1) + 1)
        return all(self.clear_point(x1 + (x2 - x1) * i / (n - 1), y1 + (y2 - y1) * i / (n - 1), hw, net, {layer}, gap)
                   for i in range(n))

def on_board(x, y, margin):
    bx, by = x - OX, y - OY
    W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
    d = min(bx, W - bx, H - by, by)
    for cx in (R, W - R):
        if by < R and ((cx == R and bx < R) or (cx == W - R and bx > W - R)):
            d = min(d, R - math.hypot(bx - cx, by - R))
    if d < margin:
        return False
    for hx, hy in D.HOLES:                                   # M3 heads: keep vias off the bosses
        if math.hypot(bx - hx, by - (D.BOARD_H - hy)) < 3.6:
            return False
    return True

ANT = None
for z in find(board, "zone"):
    if first(z, "keepout") is not None:
        pts = [(float(p[1]), float(p[2])) for p in find(first(first(z, "polygon"), "pts"), "xy")]
        ANT = (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))

def stitch(net):
    nid = NET_ID[net]
    cu = Copper(); cu.add_board(board)
    vr, gap, tw = VIA[0] / 2, CLEARANCE, 0.4
    added = tracks = 0
    def ok_via(x, y):
        if not on_board(x, y, 0.6):
            return False
        if ANT and ANT[0] - 0.5 <= x <= ANT[2] + 0.5 and ANT[1] <= y <= ANT[3] + 0.5:
            return False
        return cu.clear_point(x, y, vr, net, {"F.Cu", "B.Cu"}, gap)
    def add_via(x, y):
        board.append([Sym("via"), [Sym("at"), round(x, 4), round(y, 4)], [Sym("size"), VIA[0]], [Sym("drill"), VIA[1]],
                      [Sym("layers"), "F.Cu", "B.Cu"], [Sym("net"), nid], [Sym("uuid"), uid()]])
        cu.vias.append((x, y, vr, net))
    missing = []
    for p in [p for p in cu.pads if p["net"] == net and p["smd"] and "F.Cu" in p["layers"]]:
        best = None
        for dist in [x / 10 for x in range(6, 31, 2)]:
            for k in range(16):
                a = 2 * math.pi * k / 16
                ex = dist + max(p["w"], p["h"]) / 2
                vx, vy = p["x"] + ex * math.cos(a), p["y"] + ex * math.sin(a)
                if ok_via(vx, vy) and cu.clear_track(p["x"], p["y"], vx, vy, tw / 2, net, "F.Cu", gap):
                    best = (vx, vy); break
            if best:
                break
        if not best:
            missing.append(p["ref"]); continue
        board.append([Sym("segment"), [Sym("start"), round(p["x"], 4), round(p["y"], 4)], [Sym("end"), round(best[0], 4), round(best[1], 4)],
                      [Sym("width"), tw], [Sym("layer"), "F.Cu"], [Sym("net"), nid], [Sym("uuid"), uid()]])
        cu.tracks.append((p["x"], p["y"], best[0], best[1], tw / 2, "F.Cu", net))
        add_via(*best); added += 1; tracks += 1
    # stitching grid, 4 mm, wherever a via fits
    grid = 0
    y = OY + 2.0
    while y < OY + D.BOARD_H - 1.0:
        x = OX + 2.0
        while x < OX + D.BOARD_W - 1.0:
            if ok_via(x, y) and all(math.hypot(x - v[0], y - v[1]) > 2.5 for v in cu.vias if v[3] == net):
                add_via(x, y); grid += 1
            x += 4.0
        y += 4.0
    print("%s: %d pad vias (with tracks), %d stitching vias%s" % (net, added, grid,
          "; no room next to " + ", ".join(sorted(set(missing))) if missing else ""))

for net in POUR_NETS:
    stitch(net)

board.append(tail)
open(PCB, "w").write(dump(board) + "\n")
print("wrote %s" % PCB)
