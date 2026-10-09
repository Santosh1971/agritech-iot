"""Build ASC-Mini.kicad_pcb from tools/netlist.json, in plain python (no pcbnew).

What it does:
  - 2-layer board, 97.4 x 47.0 mm, top corners R15 (kit architecture §8), 3 x M3 holes
  - places every footprint: connectors, MCU and relays by hand (placement table below);
    small parts by a packer that puts each one as close as it can to the pins it serves
  - checks every courtyard against the outline, the screw bosses, the ESP32 antenna
    keep-out and every other courtyard, and stops if anything collides
  - GND zones on both layers (unfilled: press B in KiCad), the antenna keep-out as a rule area,
    silkscreen labels (port names, LED names, LOW VOLTAGE ONLY) and the board's name on the back
Routing is left as ratsnest, for Freerouting (see README).

    KICAD_FOOTPRINT_DIR=/path/to/kicad/footprints python3 tools/gen_pcb.py
"""
import copy, json, math, os, sys, uuid
import sexpr
from sexpr import Sym, find, first, dump
import fplib
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
OX, OY = 100.0, 60.0              # board origin (top-left corner) on the KiCad page
W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
CLEAR = 0.25                      # gap between courtyards

def uid():
    return str(uuid.uuid4())

parts = {p["ref"]: p for p in json.load(open(os.path.join(HERE, "netlist.json")))}
fps = {ref: fplib.load(p["fp"]) for ref, p in parts.items()}

# ---- board geometry (board coordinates: x right, y down, origin top-left) --------------------
def inside_board(x, y):
    if not (-1e-6 <= x <= W + 1e-6 and -1e-6 <= y <= H + 1e-6):
        return False
    for cx in (R, W - R):
        if y < R and ((x < R and cx == R) or (x > W - R and cx == W - R)):
            if math.hypot(x - cx, y - R) > R + 1e-6:
                return False
    return True

# Holes are given from the bottom-left corner with y up; convert.
HOLES = [(x, H - y) for x, y in D.HOLES]
KEEP = [(x - 3.45, y - 3.45, x + 3.45, y + 3.45) for x, y in HOLES]   # M3 screw head + washer

# ---- placement ------------------------------------------------------------------------------
placed = {}      # ref -> dict(x, y, rot, back, box)

def overlaps(a, b, gap=CLEAR):
    return a[0] < b[2] + gap and b[0] < a[2] + gap and a[1] < b[3] + gap and b[1] < a[3] + gap

def is_tht(ref):
    return any(first(p, "type") is None and p[2] in ("thru_hole", "np_thru_hole") for p in find(fps[ref], "pad"))

def box_of(ref, x, y, rot, back=False):
    if ref == "U1":                # module body only; the antenna keep-out is checked as a rule area
        return (x - 7.9, y - 9.95, x + 7.9, y + 10.75)
    return fplib.placed_box(fps[ref], x, y, rot, back)

def conflicts(ref, box, back):
    for o, q in placed.items():
        same_side = q["back"] == back
        if same_side or is_tht(o) or is_tht(ref):
            if overlaps(box, q["box"]):
                return o
    for k in KEEP:
        if overlaps(box, k, 0):
            return "screw boss"
    if not back and ANTENNA and ref != "U1" and overlaps(box, ANTENNA, 0):
        return "antenna keep-out"
    for t in TEXT_BOXES:
        if not back and overlaps(box, t, 0):
            return "silkscreen label"
    return None

ROUND = {"BZ1"}                    # round bodies: test the circle, not the box corners

def fits_board(ref, box):
    if ref in ROUND:
        cx, cy, r = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, (box[2] - box[0]) / 2
        return all(inside_board(cx + r * math.cos(a / 8 * math.pi), cy + r * math.sin(a / 8 * math.pi)) for a in range(16))
    if ref == "J2":                # USB-C overhangs the left edge on purpose
        box = (max(box[0], 0), box[1], box[2], box[3])
    corners = [(box[0], box[1]), (box[2], box[1]), (box[2], box[3]), (box[0], box[3])]
    return all(inside_board(cx, cy) for cx, cy in corners)

def put(ref, cx, cy, rot=0, back=False):
    """Place `ref` so the centre of its (rotated) courtyard lands on (cx, cy)."""
    b = box_of(ref, 0, 0, rot, back)
    x, y = cx - (b[0] + b[2]) / 2, cy - (b[1] + b[3]) / 2
    box = box_of(ref, x, y, rot, back)
    hit = conflicts(ref, box, back)
    if hit or not fits_board(ref, box):
        sys.exit("cannot place %s at (%.2f, %.2f): %s" % (ref, cx, cy, hit or "off the board"))
    placed[ref] = dict(x=x, y=y, rot=rot, back=back, box=box)

def pack(ref, ax, ay, regions, rots=(0, 90), back=False):
    """Put `ref` in one of `regions` (each x0, y0, x1, y1), as close to (ax, ay) as it fits."""
    best = None
    step = 0.25
    if isinstance(regions[0], (int, float)):
        regions = [regions]
    for region, rot in [(g, r) for g in regions for r in rots]:
        b = box_of(ref, 0, 0, rot, back)
        w, h = b[2] - b[0], b[3] - b[1]
        nx = int((region[2] - region[0] - w) / step) + 1
        ny = int((region[3] - region[1] - h) / step) + 1
        for i in range(max(nx, 0)):
            for j in range(max(ny, 0)):
                x0, y0 = region[0] + i * step, region[1] + j * step
                cx, cy = x0 + w / 2, y0 + h / 2
                d = math.hypot(cx - ax, cy - ay)
                if best and d >= best[0]:
                    continue
                box = (x0, y0, x0 + w, y0 + h)
                if conflicts(ref, box, back) or not fits_board(ref, box):
                    continue
                best = (d, x0 - b[0], y0 - b[1], rot, box)
    if not best:
        json.dump({r: q["box"] for r, q in placed.items()}, open(os.path.join(HERE, "placement_debug.json"), "w"))
        sys.exit("no room for %s in %s" % (ref, regions))
    placed[ref] = dict(x=best[1], y=best[2], rot=best[3], back=back, box=best[4])

# Antenna keep-out: the module's own rule area, from the footprint (y -24.75..-5.25 around its origin).
ESP_X, ESP_Y = 60.8, 9.75          # module origin; its antenna end (y - 9.75) sits on the top edge
ANTENNA = (ESP_X - 22.7, -1.0, ESP_X + 22.7, ESP_Y - 5.25)

# Silkscreen labels that need clear board, reserved before parts are packed.
TEXTS = []        # (text, x, y, size, angle, layer)
TEXT_BOXES = []
def label(text, x, y, size=1.0, angle=0, layer="F.SilkS", reserve=True):
    TEXTS.append((text, x, y, size, angle, layer))
    if reserve and layer == "F.SilkS":
        w = 0.62 * size * len(text) + 0.3
        h = size + 0.4
        if angle in (90, 270):
            w, h = h, w
        TEXT_BOXES.append((x - w / 2, y - h / 2, x + w / 2, y + h / 2))

# -- fixed parts --------------------------------------------------------------------------------
for ref, (x, y) in zip(("H1", "H2", "H3"), HOLES):
    placed[ref] = dict(x=x, y=y, rot=0, back=False, box=(x - 3.45, y - 3.45, x + 3.45, y + 3.45))
KEEP = []                          # the holes are now placed parts; keep-out handled as overlaps

placed["U1"] = dict(x=ESP_X, y=ESP_Y, rot=0, back=False, box=box_of("U1", ESP_X, ESP_Y, 0))

# Bottom edge: screw terminals, wire entry facing the glands (below)
put("J1", 15.6, 40.5)
put("J9", 68.7, 40.5)
put("J10", 82.2, 40.5)
# Second row: sensor ports and the Grove port
for i, ref in enumerate(("J4", "J5", "J6", "J7")):
    put(ref, 16.2 + 13.9 * i, 30.35)
put("J8", 71.2, 30.65)
# Relays to the right of the module, contacts towards the OUT terminals
put("K1", 83.5, 11.6)
put("K2", 83.5, 20.6)
# USB-C on the left edge, opening outwards
put("J2", 4.3, 21.5, rot=-90)
# Buzzer in the rounded top-left corner (round, 12 mm)
put("BZ1", 11.0, 8.9)
# Buttons on the top edge, where a finger reaches them with the lid open
put("SW1", 23.0, 4.6)
put("SW2", 32.0, 4.6)

# Silkscreen labels with reserved space
for ref, name in (("J4", "S1"), ("J5", "S2"), ("J6", "S3"), ("J7", "S4"), ("J8", "I2C-1")):
    b = placed[ref]["box"]
    label(name, (b[0] + b[2]) / 2, b[1] - 0.85, 1.0)
for ref, name in (("SW1", "RST"), ("SW2", "PAIR")):
    b = placed[ref]["box"]
    label(name, (b[0] + b[2]) / 2, b[3] + 0.8, 1.0)
label("LOW VOLTAGE ONLY", 91.6, 42.4, 0.8, 90)
label("COM NO | COM NO", 93.4, 42.4, 0.8, 90)
label("12V IN", 4.6, 42.4, 1.0, 90)

# -- small parts, each near what it serves ---------------------------------------------------------
UPPER_LEFT = (0, 0, 53.8, 26.3)
BELOW_ESP = (52.6, 20.9, 72.7, 26.6)
BESIDE_RELAYS = (68.6, 4.6, 72.75, 26.6)
UNDER_RELAYS = (72.0, 24.8, 89.2, 34.0)
BUCK = (22.4, 33.9, 61.9, 47.0)
PORTS = (9.0, 20.5, 52.6, 26.4)
RIGHT_LOW = (88.8, 25.0, 97.4, 29.6)

def pin_xy(ref, num):
    q = placed[ref]
    for p in find(fps[ref], "pad"):
        if p[1] == num:
            at = first(p, "at")
            dx, dy = fplib.rot(float(at[1]), float(at[2]), q["rot"])
            return q["x"] + dx, q["y"] + dy
    raise KeyError(ref + "." + num)

# 12 V input and buck, between J1 and the OUT terminals
for ref in ("D1", "D2", "C1", "C2", "C3", "U2", "C4", "L1", "R1", "R2", "C5", "C6", "D3", "R5"):
    pack(ref, 30.0, 40.5, BUCK)
# Sensor port protection: series resistor and clamp next to each port
for i in range(1, 5):
    j = "J%d" % (3 + i)
    px, py = pin_xy(j, "4")
    pack("R%d" % (17 + i), px, 24.5, PORTS)
    pack("D%d" % (7 + i), px - 3, 24.5, PORTS)
for ref in ("F2", "C16", "F3", "C17"):
    pack(ref, 40.0, 24.5, PORTS)
# Relay drivers: OUT1's beside the module, OUT2's under the relays
for i, k in ((1, "K1"), (2, "K2")):
    cx, cy = pin_xy(k, "2")
    r = 23 + (i - 1) * 3
    regions = [BESIDE_RELAYS, BELOW_ESP] if i == 1 else [UNDER_RELAYS, BELOW_ESP, RIGHT_LOW]
    for ref in ("Q%d" % (1 + i), "D%d" % (11 + i), "R%d" % (r + 1), "R%d" % (r + 2), "R%d" % (r + 3), "D%d" % (13 + i)):
        pack(ref, cx - 3, cy + (0 if i == 1 else 4), regions, rots=(90, 0) if i == 1 else (0, 90))
pack("R22", 50.0, 23.0, UPPER_LEFT)
pack("R23", 50.0, 23.0, UPPER_LEFT)
# USB: CC resistors, fuse, OR diode, ESD next to the connector
for ref in ("U3", "R3", "R4", "F1", "D4"):
    pack(ref, 12.0, 21.0, UPPER_LEFT)
# 3.3 V regulator and its capacitors
for ref in ("U4", "C8", "C9", "C10", "C11"):
    pack(ref, 30.0, 12.0, UPPER_LEFT)
# Module support: decoupling at its 3V3 pin, EN, PAIR, BOARD_ID, VIN sense, LEDs
for ref in ("C12", "C13"):
    pack(ref, 52.0, 6.0, UPPER_LEFT, rots=(90, 0))
for ref in ("R8", "C14", "R9", "R10", "R11", "R6", "C7"):
    pack(ref, 44.0, 22.0, UPPER_LEFT)
for ref, name in (("D5", "PWR"), ("D6", "STATUS")):
    pack(ref, 22.0 if ref == "D5" else 30.0, 9.6, UPPER_LEFT, rots=(0,))
    b = placed[ref]["box"]
    label(name, (b[0] + b[2]) / 2, b[3] + 0.75, 0.8, reserve=False)
for ref in ("R7", "R12"):
    pack(ref, 26.0, 12.0, UPPER_LEFT)
# RTC
for ref in ("U5", "C15", "R15", "R16", "R17"):
    pack(ref, 40.0, 18.0, UPPER_LEFT)
# Buzzer and its driver
pack("Q1", 16.0, 13.0, UPPER_LEFT)
gx, gy = pin_xy("Q1", "1")                    # the gate resistor and pull-down sit on Q1's gate
for ref in ("R13", "R14", "D7"):
    pack(ref, gx, gy, UPPER_LEFT)
# Test header for the production tester
pack("J3", ESP_X - 4.0, ESP_Y + 4.0, (ESP_X - 8.5, ESP_Y - 3.5, ESP_X + 8.5, ESP_Y + 11.0), rots=(90, 0), back=True)
# OUT LEDs: labels next to them
for ref, name in (("D14", "OUT1"), ("D15", "OUT2")):
    b = placed[ref]["box"]
    label(name, (b[0] + b[2]) / 2, b[3] + 0.7, 0.8, reserve=False)
# RTC cell on the back (flat SMD holder), behind the upper-left area where the front is all SMD
pack("BT1", 31.0, 13.0, (17.5, 0.3, 45.2, 26.6), rots=(0,), back=True)

# 12 V polarity marks over J1's two pins
for num, mark in (("1", "+"), ("2", "-")):
    x, y = pin_xy("J1", num)
    label(mark, x, placed["J1"]["box"][1] - 0.9, 1.2, reserve=False)

# Late moves, after everything else is packed (so no other part shifts): the BOARD_ID divider goes
# to the back, under the module next to GPIO11, where the front left of the module is too crowded
# for its GND pad to reach the ground.
for ref in ("R10", "R11"):
    del placed[ref]
for ref in ("R10", "R11"):
    pack(ref, ESP_X - 6.0, ESP_Y + 6.0, (ESP_X - 8.6, ESP_Y - 3.5, ESP_X + 8.6, ESP_Y + 11.5), rots=(90, 0), back=True)

missing = set(parts) - set(placed)
if missing:
    sys.exit("not placed: " + ", ".join(sorted(missing)))

# Back-side identification
TEXTS.append(("ASC Student Kit - Mini rev A", 42.0, 38.5, 1.5, 0, "B.SilkS"))
TEXTS.append(("Agri Sensors and Controls", 42.0, 41.5, 1.0, 0, "B.SilkS"))
TEXTS.append(("agrisenseandcontrol.in", 42.0, 43.5, 1.0, 0, "B.SilkS"))

# ---- write the board -----------------------------------------------------------------------------
NETS = [""]
for p in parts.values():
    for n in list(p["pads"].values()) + list(p["nc"].values()):
        if n not in NETS:
            NETS.append(n)
NET_ID = {n: i for i, n in enumerate(NETS)}

def flip_layer(name):
    return name.replace("F.", "\0").replace("B.", "F.").replace("\0", "B.")

def transform(node, back, rot):
    """Rewrite a footprint item in place for the back side and the footprint's rotation."""
    if not isinstance(node, list):
        return
    head = node[0] if node else None
    for e in node:
        transform(e, back, rot)
    if back:
        if head in ("start", "end", "mid", "center", "xy"):
            node[2] = -float(node[2])
        if head == "layer":
            node[1] = flip_layer(node[1])
        if head == "layers":
            node[1:] = [flip_layer(l) for l in node[1:]]
    if head in ("pad", "property", "fp_text"):
        at = first(node, "at")
        if at is not None:
            x, y = float(at[1]), float(at[2])
            a = float(at[3]) if len(at) > 3 else 0.0
            if back:
                y, a = -y, -a
            at[1:] = [x, y, (a + rot) % 360]
            if back and head in ("property", "fp_text"):
                eff = first(node, "effects")
                if eff is not None and not any(isinstance(e, list) and e[0] == "justify" for e in eff):
                    eff.append([Sym("justify"), Sym("mirror")])

def footprint(ref):
    p, q = parts[ref], placed[ref]
    src = copy.deepcopy(fps[ref])
    body = [e for e in src[2:] if not (isinstance(e, list) and e[0] in ("version", "generator", "generator_version", "layer", "zone"))]
    for e in body:
        transform(e, q["back"], q["rot"])
    out = [Sym("footprint"), p["fp"], [Sym("layer"), "B.Cu" if q["back"] else "F.Cu"], [Sym("uuid"), uid()],
           [Sym("at"), round(OX + q["x"], 4), round(OY + q["y"], 4), q["rot"] % 360]]
    for e in body:
        if isinstance(e, list) and e[0] == "property":
            if e[1] == "Reference":
                e[2] = ref
            elif e[1] == "Value":
                e[2] = p["value"]
            elif e[1] == "Footprint":
                e[2] = p["fp"]
        if isinstance(e, list) and e[0] == "fp_text" and len(e) > 2 and e[1] in ("reference", "value"):
            e[2] = ref if e[1] == "reference" else p["value"]
        if isinstance(e, list) and e[0] == "pad":
            n = p["pads"].get(e[1]) or p["nc"].get(e[1])
            if n:
                lay = [i for i, x in enumerate(e) if isinstance(x, list) and x[0] == "layers"][0]
                e.insert(lay + 1, [Sym("net"), NET_ID[n], n])
        out.append(e)
    # schematic link, after the properties
    idx = max(i for i, e in enumerate(out) if isinstance(e, list) and e[0] == "property") + 1
    out[idx:idx] = [[Sym("path"), "/" + p["uuid"]], [Sym("sheetname"), "/"], [Sym("sheetfile"), D.PROJECT + ".kicad_sch"]]
    return out

def gr_line(a, b, layer, w):
    return [Sym("gr_line"), [Sym("start"), OX + a[0], OY + a[1]], [Sym("end"), OX + b[0], OY + b[1]],
            [Sym("stroke"), [Sym("width"), w], [Sym("type"), Sym("default")]], [Sym("layer"), layer], [Sym("uuid"), uid()]]

def gr_arc(a, m, b, layer, w):
    return [Sym("gr_arc"), [Sym("start"), OX + a[0], OY + a[1]], [Sym("mid"), OX + m[0], OY + m[1]], [Sym("end"), OX + b[0], OY + b[1]],
            [Sym("stroke"), [Sym("width"), w], [Sym("type"), Sym("default")]], [Sym("layer"), layer], [Sym("uuid"), uid()]]

k = R * (1 - math.sqrt(0.5))
edge = [gr_arc((0, R), (k, k), (R, 0), "Edge.Cuts", 0.05), gr_line((R, 0), (W - R, 0), "Edge.Cuts", 0.05),
        gr_arc((W - R, 0), (W - k, k), (W, R), "Edge.Cuts", 0.05), gr_line((W, R), (W, H), "Edge.Cuts", 0.05),
        gr_line((W, H), (0, H), "Edge.Cuts", 0.05), gr_line((0, H), (0, R), "Edge.Cuts", 0.05)]

def outline_poly(inset=0.0):
    pts = []
    for cx, a0, a1 in ((R, 180, 270), (W - R, 270, 360)):
        for i in range(9):
            a = math.radians(a0 + (a1 - a0) * i / 8)
            pts.append((cx + (R - inset) * math.cos(a), R + (R - inset) * math.sin(a)))
    pts += [(W - inset, H - inset), (inset, H - inset)]
    return pts

def zone(net, layers, pts, name=None, keepout=False):
    z = [Sym("zone"), [Sym("net"), NET_ID.get(net, 0)], [Sym("net_name"), net],
         [Sym("layers")] + list(layers) if len(layers) > 1 else [Sym("layer"), layers[0]], [Sym("uuid"), uid()]]
    if name:
        z.append([Sym("name"), name])
    z += [[Sym("hatch"), Sym("edge"), 0.5]]
    if keepout:
        z += [[Sym("connect_pads"), [Sym("clearance"), 0]], [Sym("min_thickness"), 0.25], [Sym("filled_areas_thickness"), Sym("no")],
              [Sym("keepout"), [Sym("tracks"), Sym("not_allowed")], [Sym("vias"), Sym("not_allowed")], [Sym("pads"), Sym("not_allowed")],
               [Sym("copperpour"), Sym("not_allowed")], [Sym("footprints"), Sym("allowed")]]]
    else:
        z += [[Sym("connect_pads"), [Sym("clearance"), 0.3]], [Sym("min_thickness"), 0.25], [Sym("filled_areas_thickness"), Sym("no")],
              [Sym("fill"), [Sym("thermal_gap"), 0.4], [Sym("thermal_bridge_width"), 0.4]]]
    z.append([Sym("polygon"), [Sym("pts")] + [[Sym("xy"), round(OX + x, 4), round(OY + y, 4)] for x, y in pts]])
    return z

def text(t, x, y, size, angle, layer):
    eff = [Sym("effects"), [Sym("font"), [Sym("size"), size, size], [Sym("thickness"), round(size * 0.15, 3)]]]
    if layer.startswith("B."):
        eff.append([Sym("justify"), Sym("mirror")])
    return [Sym("gr_text"), t, [Sym("at"), round(OX + x, 4), round(OY + y, 4), angle], [Sym("layer"), layer], [Sym("uuid"), uid()], eff]

header = sexpr.parse(open(os.path.join(HERE, "board_header.kicad_pcb")).read())
board = header + [[Sym("net"), i, n] for i, n in enumerate(NETS)]
board += [footprint(ref) for ref in sorted(placed, key=lambda r: (r.rstrip("0123456789"), int("".join(c for c in r if c.isdigit()) or 0)))]
board += edge
a = ANTENNA
board.append(zone("", ["F.Cu", "B.Cu"], [(max(a[0], 0), 0), (min(a[2], W), 0), (min(a[2], W), a[3]), (max(a[0], 0), a[3])],
                  name="ESP32 antenna keep-out", keepout=True))
board.append(zone("GND", ["F.Cu"], outline_poly(0.3), name="GND top"))
board.append(zone("GND", ["B.Cu"], outline_poly(0.3), name="GND bottom"))
board += [text(*t) for t in TEXTS]
board.append([Sym("embedded_fonts"), Sym("no")])
open(os.path.join(PRJ, D.PROJECT + ".kicad_pcb"), "w").write(dump(board) + "\n")
json.dump({r: dict(x=round(q["x"], 3), y=round(q["y"], 3), rot=q["rot"], back=q["back"], box=[round(v, 3) for v in q["box"]])
           for r, q in placed.items()}, open(os.path.join(HERE, "placement.json"), "w"), indent=1)
print("board: %d footprints, %d nets, %d labels" % (len(placed), len(NETS) - 1, len(TEXTS)))
