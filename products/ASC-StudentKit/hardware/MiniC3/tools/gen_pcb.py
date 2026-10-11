"""Build <PROJECT>.kicad_pcb from tools/netlist-<PROJECT>.json and the placement in the design module.

Runs in KiCad's own python (it needs pcbnew). DESIGN picks the design module (default: design,
the Mini; design_aht20 for the sensor board):

    KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
    DESIGN=design $KP tools/gen_pcb.py

What it does: two-layer board of D.BOARD_W x D.BOARD_H, holes and slots, every footprint placed
from D.PLACE (the centre of its courtyard on the given point), pads joined to their nets, GND
pours on both layers with no-pour areas from D.NO_POUR, and D.silk() labels. Routing is route.py.
"""
import importlib, json, os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
sys.path.insert(0, HERE)
D = importlib.import_module(os.environ.get("DESIGN", "design"))

FPROOT = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
OX, OY = 100.0, 60.0                       # board top-left corner on the page, mm
W, H, R = D.BOARD_W, D.BOARD_H, D.CORNER_R
mm = pcbnew.FromMM

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

parts = json.load(open(os.path.join(HERE, "netlist-%s.json" % D.PROJECT)))
board = pcbnew.NewBoard(os.path.join(PRJ, D.PROJECT + ".kicad_pcb"))

# ---- design rules: JLC / WhyPCB standard 2-layer process with margin
ds = board.GetDesignSettings()
ds.m_TrackMinWidth = mm(0.2)
ds.m_MinClearance = mm(0.2)
ds.m_ViasMinSize = mm(0.6)
ds.m_MinThroughDrill = mm(0.3)
ds.m_CopperEdgeClearance = mm(0.4)        # V-cut panel: copper keeps 0.4 mm from the edge
ds.m_MinSilkTextHeight = mm(0.7)           # assembly labels are 0.7 mm, as on the WPC board
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(mm(0.2)); nc.SetTrackWidth(mm(0.3)); nc.SetViaDiameter(mm(0.6)); nc.SetViaDrill(mm(0.3))

# ---- nets
nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]
for p in parts:
    for n in list(p["pads"].values()) + list(p["nc"].values()):
        net(n)

# ---- outline (square corners when R = 0, as V-cut panels need), slots
def seg(a, b, layer=pcbnew.Edge_Cuts, w=0.05):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a)); s.SetEnd(pt(*b)); s.SetLayer(layer); s.SetWidth(mm(w)); board.Add(s)
def arc(c, start, ang):
    s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetCenter(pt(*c)); s.SetStart(pt(*start)); s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T), True)
    s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.05)); board.Add(s)
seg((R, 0), (W - R, 0)); seg((W, R), (W, H - R)); seg((W - R, H), (R, H)); seg((0, H - R), (0, R))
if R:
    arc((R, R), (0, R), 90); arc((W - R, R), (W - R, 0), 90); arc((W - R, H - R), (W, H - R), 90); arc((R, H - R), (R, H), 90)
for x0, y0, x1, y1 in getattr(D, "SLOTS", []):
    seg((x0, y0), (x1, y0)); seg((x1, y0), (x1, y1)); seg((x1, y1), (x0, y1)); seg((x0, y1), (x0, y0))

# ---- footprints
def load(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(PRJ, "ASC.pretty") if lib == "ASC" else os.path.join(FPROOT, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        sys.exit("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp

def courtyard_box(fp):
    fp.BuildCourtyardCaches()
    poly = fp.GetCourtyard(pcbnew.F_CrtYd)
    if poly.OutlineCount():
        return poly.BBox()
    return fp.GetBoundingBox(False)

placed = {}
holes = iter(D.HOLES)
for p in parts:
    ref = p["ref"]
    fp = load(p["fp"])
    fp.SetReference(ref)
    fp.SetValue(p["value"])
    fp.SetPath(pcbnew.KIID_PATH("/" + p["uuid"]))
    board.Add(fp)
    if ref.startswith("H"):
        fp.SetPosition(pt(*next(holes)))
    else:
        if ref not in D.PLACE:
            sys.exit("no placement for " + ref)
        x, y, rot = D.PLACE[ref]
        fp.SetPosition(pt(0, 0))
        fp.SetOrientationDegrees(rot)
        c = courtyard_box(fp).GetCenter()
        fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x) - (c.x - mm(OX)), mm(OY + y) - (c.y - mm(OY))))
    for pad in fp.Pads():
        n = p["pads"].get(pad.GetNumber()) or p["nc"].get(pad.GetNumber())
        if n:
            pad.SetNet(nets[n])
    # reference designators small, on the fab layer (the silkscreen carries function names instead)
    fp.Reference().SetLayer(pcbnew.F_Fab)
    fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.6), mm(0.6)))
    fp.Reference().SetTextThickness(mm(0.09))
    placed[ref] = fp

# ---- courtyard and outline sanity check (DRC repeats it; this fails early with names)
boxes = {r: courtyard_box(f) for r, f in placed.items()}
edge = pcbnew.BOX2I(pt(0, 0), pcbnew.VECTOR2I(mm(W), mm(H)))
bad = []
for r, b in boxes.items():
    if not edge.Contains(b.GetOrigin()) or not edge.Contains(b.GetEnd()):
        bad.append("%s off the board" % r)
for r, f in placed.items():            # a plug-in module's courtyard is its socket strips: parts between them sit under it
    if f.GetFPID().GetLibItemName() == "ESP32-C3_SuperMini_Socket":
        u = boxes.pop(r)
        for i, x in enumerate((u.GetX(), u.GetRight() - mm(2.8))):
            boxes["%s/%d" % (r, i)] = pcbnew.BOX2I(pcbnew.VECTOR2I(x, u.GetY()), pcbnew.VECTOR2I(mm(2.8), u.GetHeight()))
refs = sorted(boxes)
for i, a in enumerate(refs):
    for c in refs[i + 1:]:
        if boxes[a].Intersects(boxes[c]):
            bad.append("%s overlaps %s" % (a, c))

# ---- silkscreen, through the design's silk() hook
class Silk:
    @staticmethod
    def text(t, x, y, size=0.8, angle=0, layer="F", bold=False):
        s = pcbnew.PCB_TEXT(board)
        s.SetText(t); s.SetPosition(pt(x, y)); s.SetLayer(pcbnew.B_SilkS if layer == "B" else pcbnew.F_SilkS)
        s.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
        s.SetTextAngleDegrees(angle)
        if layer == "B":
            s.SetMirrored(True)
        board.Add(s)
    @staticmethod
    def pad_xy(ref, num):
        for pad in placed[ref].Pads():
            if pad.GetNumber() == num:
                v = pad.GetPosition()
                return pcbnew.ToMM(v.x) - OX, pcbnew.ToMM(v.y) - OY
        raise KeyError(ref + "." + num)
    @staticmethod
    def box_mm(ref):
        b = courtyard_box(placed[ref])
        return (pcbnew.ToMM(b.GetX()) - OX, pcbnew.ToMM(b.GetY()) - OY,
                pcbnew.ToMM(b.GetRight()) - OX, pcbnew.ToMM(b.GetBottom()) - OY)
D.silk(Silk)

# ---- assembly labels for hand soldering (house style, as on WPC / FG1): "R3 1K", "U3 ULN2003",
# a "1" at IC pin 1 and a "+" at LED / diode anodes. Each label goes to the first free spot around its
# part: clear of pads, part bodies (fab outline), other labels, holes and the board edge.
LBL = 0.7
def bbox_mm(item):
    b = item.GetBoundingBox()
    return (pcbnew.ToMM(b.GetX()) - OX, pcbnew.ToMM(b.GetY()) - OY, pcbnew.ToMM(b.GetRight()) - OX, pcbnew.ToMM(b.GetBottom()) - OY)
def grow(b, d):
    return (b[0] - d, b[1] - d, b[2] + d, b[3] + d)
def hit(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]

obst = []                                   # (box, owner ref)
for ref, f in placed.items():
    for p in f.Pads():
        obst.append((grow(bbox_mm(p), 0.15), ref))
    if f.GetFPID().GetLibItemName() == "ESP32-C3_SuperMini_Socket":
        continue                            # under the module stays usable (it plugs in after soldering)
    fab = [g for g in f.GraphicalItems() if g.GetLayer() == pcbnew.F_Fab and g.GetClass() != "PCB_TEXT"]
    if fab:
        xs = [bbox_mm(g) for g in fab]
        obst.append(((min(b[0] for b in xs), min(b[1] for b in xs), max(b[2] for b in xs), max(b[3] for b in xs)), ref))
    else:
        obst.append((Silk.box_mm(ref), ref))
for t in board.GetDrawings():
    if t.GetClass() == "PCB_TEXT" and t.GetLayer() == pcbnew.F_SilkS:
        obst.append((grow(bbox_mm(t), 0.1), "text"))

def place_text(txt, cands, owner, size=LBL):
    """cands: list of (x, y, angle). Returns True when placed."""
    for x, y, ang in cands:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt); t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(0.12))
        t.SetTextAngleDegrees(ang); t.SetPosition(pt(x, y))
        b = bbox_mm(t)
        if b[0] < 0.3 or b[1] < 0.3 or b[2] > W - 0.3 or b[3] > H - 0.3:
            continue
        if any(hit(b, o) for o, who in obst):
            continue
        board.Add(t)
        obst.append((grow(b, 0.1), "text"))
        return True
    return False

def around(ref, w_hint):
    """Candidate centres around a part: above, below, left, right (horizontal), then vertical left/right."""
    x0, y0, x1, y1 = Silk.box_mm(ref)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    h = LBL / 2 + 0.25
    c = []
    for dx in (0, -1.5, 1.5, -3, 3):
        c += [(cx + dx, y0 - h, 0), (cx + dx, y1 + h, 0)]
    for dy in (0, -1.5, 1.5):
        c += [(x0 - w_hint / 2 - 0.2, cy + dy, 0), (x1 + w_hint / 2 + 0.2, cy + dy, 0)]
    c += [(x0 - h, cy, 90), (x1 + h, cy, 90), (x0 - h, cy, 270), (x1 + h, cy, 270)]
    for k in (1, 2):                                       # a second ring, a little further out
        c += [(cx, y0 - h - 0.9 * k, 0), (cx, y1 + h + 0.9 * k, 0)]
    return c

failed = []
values = getattr(D, "ASSEMBLY_VALUE", {})
order = sorted((r for r in placed if not r.startswith("H") and r in values), key=lambda r: (r[0] != "U", r))
inside = getattr(D, "LABEL_INSIDE", set())
for ref in order:
    txt = ("%s %s" % (ref, values[ref])).strip()
    if ref in inside:                                      # on the part's own body: drop its body from the obstacles
        body = [o for o in obst if o[1] == ref and o[0] == max((q for q in obst if q[1] == ref), key=lambda q: (q[0][2]-q[0][0])*(q[0][3]-q[0][1]))[0]]
        for o in body:
            obst.remove(o)
        x0, y0, x1, y1 = body[0][0] if body else Silk.box_mm(ref)
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        ok = place_text(txt, [(cx, cy, 0), (cx, cy - 1.2, 0), (cx, cy + 1.2, 0), (cx - 3, cy, 0), (cx + 3, cy, 0)], ref)
        obst.extend(body)
        if not ok:
            failed.append(ref)
        continue
    if not place_text(txt, around(ref, 0.62 * LBL * len(txt)), ref):
        failed.append(ref)

def mark(ref, pad_num, txt):
    """A small mark ('1' or '+') just beyond a pad, on the side away from the part's centre."""
    f = placed[ref]
    pad = [p for p in f.Pads() if p.GetNumber() == pad_num][0]
    px, py = pcbnew.ToMM(pad.GetPosition().x) - OX, pcbnew.ToMM(pad.GetPosition().y) - OY
    cx, cy = [(a + b) / 2 for a, b in zip(Silk.box_mm(ref)[:2], Silk.box_mm(ref)[2:])]
    pb = bbox_mm(pad)
    hw, hh = (pb[2] - pb[0]) / 2, (pb[3] - pb[1]) / 2
    vx, vy = px - cx, py - cy
    cands = []
    if abs(vx) >= abs(vy):                                 # pad on the left / right of the part
        sx = 1 if vx > 0 else -1
        cands += [(px + sx * (hw + o), py, 0) for o in (0.5, 0.6, 0.75, 0.9, 1.1, 1.3)]
        cands += [(px, py - hh - o, 0) for o in (0.5, 0.6)] + [(px, py + hh + o, 0) for o in (0.5, 0.6)]
    else:
        sy = 1 if vy > 0 else -1
        cands += [(px, py + sy * (hh + o), 0) for o in (0.5, 0.6, 0.75, 0.9, 1.1, 1.3)]
        cands += [(px - hw - o, py, 0) for o in (0.5, 0.6)] + [(px + hw + o, py, 0) for o in (0.5, 0.6)]
    cands += [(px + dx, py + dy, 0) for dx, dy in ((-1.1, -1.1), (1.1, -1.1), (-1.1, 1.1), (1.1, 1.1), (-1.5, 0), (1.5, 0), (0, -1.5), (0, 1.5))]
    if not place_text(txt, cands, ref):
        failed.append("%s.%s(%s)" % (ref, pad_num, txt))

for ref, pad_num, txt in getattr(D, "MARKS", []):
    if ref in placed:
        mark(ref, pad_num, txt)
if failed:
    print("LABELS NOT PLACED:", ", ".join(failed))

# ---- zones: GND on both layers, and the no-pour areas
def chain(x0, y0, x1, y1):
    c = pcbnew.SHAPE_LINE_CHAIN()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        c.Append(pt(x, y))
    c.SetClosed(True)
    return c

for layer, name in ((pcbnew.F_Cu, "GND top"), (pcbnew.B_Cu, "GND bottom")):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(nets["GND"]); z.SetZoneName(name)
    z.Outline().AddOutline(chain(0.45, 0.45, W - 0.45, H - 0.45))
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.25))
    z.SetThermalReliefGap(mm(0.4)); z.SetThermalReliefSpokeWidth(mm(0.45))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL if getattr(D, "POUR_SOLID", False) else pcbnew.ZONE_CONNECTION_THERMAL)
    board.Add(z)

for i, area in enumerate(D.NO_POUR):
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True); k.SetZoneName("No pour %d" % (i + 1))
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
    k.SetLayerSet(ls)
    k.SetDoNotAllowCopperPour(True); k.SetDoNotAllowTracks(False); k.SetDoNotAllowVias(False)
    k.SetDoNotAllowPads(False); k.SetDoNotAllowFootprints(False)
    k.Outline().AddOutline(chain(*area))
    board.Add(k)

for i, area in enumerate(getattr(D, "NO_TRACK", [])):
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True); k.SetZoneName("No track %d" % (i + 1))
    ls = pcbnew.LSET(); ls.AddLayer(pcbnew.F_Cu); ls.AddLayer(pcbnew.B_Cu)
    k.SetLayerSet(ls)
    k.SetDoNotAllowCopperPour(True); k.SetDoNotAllowTracks(True); k.SetDoNotAllowVias(True)
    k.SetDoNotAllowPads(False); k.SetDoNotAllowFootprints(False)
    k.Outline().AddOutline(chain(*area))
    board.Add(k)

# ---- pre-routed, locked tracks (the 230 V contacts)
for netn, (ra, pa), (rb, pb), w in getattr(D, "PREROUTE", []):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pt(*Silk.pad_xy(ra, pa))); t.SetEnd(pt(*Silk.pad_xy(rb, pb)))
    t.SetWidth(mm(w)); t.SetLayer(pcbnew.F_Cu); t.SetNet(nets[netn]); t.SetLocked(True)
    board.Add(t)

import apply_3d
apply_3d.set_models(board)                # STEP models for parts the KiCad library has none for
board.Save(os.path.join(PRJ, D.PROJECT + ".kicad_pcb"))
print("%s: %d footprints, %d nets" % (D.PROJECT, len(placed), len(nets)))
for x in bad:
    print("PLACEMENT:", x)
