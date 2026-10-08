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
CLEARANCE = 0.2
VIA = (0.6, 0.3)
CLASSES = {
    "power": (0.6, {"VIN_RAW", "VIN", "/VIN_RAW", "/VIN", "/+5V_BUCK", "/BUCK_SW", "+5V", "/VBUS", "/VBUS_F"}),
    "supply": (0.4, {"+3V3", "GND", "/+5V_PORT", "/+3V3_PORT"}),
    "contacts": (1.0, {"/OUT1_COM", "/OUT1_NO", "/OUT2_COM", "/OUT2_NO"}),
}
DEFAULT_W = 0.25

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
    if len(pins) > 1:
        lines.append("(net %s (pins %s))" % (q(n), " ".join(pins)))
by_class = {}
for n in pin_nets:
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
    cmd = [os.environ.get("JAVA", "java"), "-jar", jar, "-de", DSN, "-do", SES, "-mp", os.environ.get("ROUTE_PASSES", "40"), "--gui.enabled=false"]
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
board.append(tail)
open(PCB, "w").write(dump(board) + "\n")
print("imported %d track segments and %d vias into %s" % (n_seg, n_via, PCB))
