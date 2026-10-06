"""Build AWD-FieldNode.kicad_pcb from netlist.json with KiCad's pcbnew module.

Run with KiCad's bundled python (see README). What it does:
  - 4-layer outline: 40 x 60 mm head + 10 x 300 mm strip
  - places every footprint (head parts by a nearest-free-spot packer)
  - routes the whole strip: pad -> via -> In2.Cu lane -> fan-out -> via -> RS series resistor
  - zones: B.Cu + In2.Cu solid GND, In1.Cu solid GND in head / 25 % hatched GND under the pads,
    F.Cu GND in the head only
  - silkscreen scale on the back, notes on Dwgs.User
Head-side routing (RS -> ESP32 touch pins, LoRa SPI, power) is left as ratsnest.
"""
import json, math, os
import pcbnew
from pcbnew import FromMM as mm, VECTOR2I
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
FPROOT = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
OUTFILE = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")

board = pcbnew.BOARD()
board.SetCopperLayerCount(4)
ds = board.GetDesignSettings()
ds.SetBoardThickness(mm(1.6))
L = dict(F=pcbnew.F_Cu, In1=pcbnew.In1_Cu, In2=pcbnew.In2_Cu, B=pcbnew.B_Cu)

def pt(x, y):
    return VECTOR2I(mm(x), mm(y))

# ---- nets -------------------------------------------------------------------------
parts = json.load(open(os.path.join(HERE, "netlist.json")))
nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]
for p in parts:
    for n in list(p["pads"].values()) + list(p.get("nc", {}).values()):
        net(n)
GND = net("GND")

# ---- outline -------------------------------------------------------------------------
def edge_line(a, b):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a)); s.SetEnd(pt(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    board.Add(s)

hx0, hy0, hx1, hy1 = D.HEAD
sx0, sx1, sy0, sy1 = D.STRIP_X0, D.STRIP_X1, D.STRIP_Y0, D.STRIP_Y1
r = (sx1 - sx0) / 2
outline = [(hx0, hy0), (hx1, hy0), (hx1, hy1), (sx1, sy0), (sx1, sy1 - r)]
for a, b in zip(outline, outline[1:]):
    edge_line(a, b)
arc = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
arc.SetArcGeometry(pt(sx1, sy1 - r), pt(D.STRIP_CX, sy1), pt(sx0, sy1 - r))
arc.SetLayer(pcbnew.Edge_Cuts); arc.SetWidth(mm(0.1)); board.Add(arc)
for a, b in zip([(sx0, sy1 - r), (sx0, sy0), (hx0, hy1)], [(sx0, sy0), (hx0, hy1), (hx0, hy0)]):
    edge_line(a, b)

# ---- footprints -----------------------------------------------------------------------
def load(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(PRJ, "AWD.pretty") if lib == "AWD" else FPROOT + lib + ".pretty"
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError("footprint not found: " + fpid)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp

fps = {}
for p in parts:
    fp = load(p["fp"])
    fp.SetReference(p["ref"]); fp.SetValue(p["value"])
    fp.SetPath(pcbnew.KIID_PATH("/" + p["uuid"]))
    for pad in fp.Pads():
        n = p["pads"].get(pad.GetNumber()) or p.get("nc", {}).get(pad.GetNumber())
        if n:
            pad.SetNet(net(n))
    fp.SetExcludedFromBOM(not p["in_bom"])
    board.Add(fp)
    fps[p["ref"]] = fp

def place(ref, x, y, rot=0):
    fp = fps[ref]
    fp.SetLocked(True)
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pt(x, y))

# Sensing pads
for i in range(1, 13):
    place("E%d" % i, D.STRIP_CX, D.pad_center_y(i))
place("E13", D.STRIP_CX, D.REF_CENTER_Y)
place("E14", D.STRIP_CX, D.GND_PAD_CENTER_Y)

# Big parts: ESP32 module with its antenna at the top edge, SX1262 with ANT facing the SMA
place("U1", D.STRIP_CX, hy0 + 10.25)            # module body 15.4 x 20.5, antenna keepout above the board
place("U2", D.STRIP_CX, 72.0)                   # SPI pads (12-15) face the ESP32's SPI pins on the right
place("J1", hx0 + 1.1, 65.0, 180)               # edge-mount SMA on the left edge next to ANT (pad 1); check seating vs datasheet

# Touch series resistors in a row at the strip neck (lane k: P12 .. P1, REF)
def lane_k(i):                                  # i = 1..12 pads, 13 = REF
    return 12 - i if i <= 12 else 12
def lane_x(k):
    return D.STRIP_CX + (k - 6) * D.LANE_PITCH
def rs_x(k):
    return D.STRIP_CX + (k - 6) * D.RS_PITCH
for i in range(1, 14):
    place("RS%d" % i, rs_x(lane_k(i)), D.RS_Y, 90)
    p1, p2 = sorted(fps["RS%d" % i].Pads(), key=lambda p: p.GetNumber())
    if p2.GetPosition().y < p1.GetPosition().y:      # want pad 1 (T side) up, pad 2 (sense side) down
        place("RS%d" % i, rs_x(lane_k(i)), D.RS_Y, 270)
    fps["RS%d" % i].Reference().SetVisible(False)   # 1.6 mm pitch: no room for designators

# Everything else: nearest free spot to a preferred location
def bbox(fp):
    c = fp.GetCourtyard(pcbnew.F_CrtYd)
    b = c.BBox() if c.OutlineCount() else fp.GetBoundingBox(False)
    return [pcbnew.ToMM(v) for v in (b.GetX(), b.GetY(), b.GetRight(), b.GetBottom())]

blocked = [
    (hx0, hy0, hx1, hy0 + 5.25),                 # ESP32 antenna keepout band
    (D.STRIP_CX - 7.9, hy0, D.STRIP_CX + 7.9, hy0 + 20.75),   # ESP32 body
    (D.STRIP_CX - 9.4, 72 - 9.1, D.STRIP_CX + 9.4, 72 + 9.1), # SX1262 module
    (hx0, 59.0, bbox(fps["J1"])[2], 71.0),       # SMA
    (rs_x(0) - 1.5, 89.5, rs_x(12) + 1.5, hy1),  # RS row, fan-out vias and lanes
]
def free(b, m=0.3):
    if b[0] < hx0 + 0.6 or b[2] > hx1 - 0.6 or b[1] < hy0 + 0.6 or b[3] > hy1 - 0.6:
        return False
    return not any(b[0] - m < o[2] and b[2] + m > o[0] and b[1] - m < o[3] and b[3] + m > o[1] for o in blocked)

def pack(ref, px, py, rot=0):
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pt(px, py))
    b0 = bbox(fp)
    best = None
    for gx in range(-80, 81):
        for gy in range(-120, 121):
            dx, dy = gx * 0.5, gy * 0.5
            d = dx * dx + dy * dy
            if best and d >= best[0]:
                continue
            b = [b0[0] + dx, b0[1] + dy, b0[2] + dx, b0[3] + dy]
            if free(b):
                best = (d, dx, dy, b)
    if not best:
        raise RuntimeError("no room for " + ref)
    _, dx, dy, b = best
    fp.SetPosition(pt(px + dx, py + dy))
    blocked.append(b)

PREF = [  # ref, preferred x, y, rotation - next to the pins they serve
    ("C3", 107.8, 47.5, 90), ("C2", 106.0, 47.5, 90),                       # ESP32 3V3 (pad 3, left top)
    ("R3", 104.0, 47.5, 90), ("SW1", 102.5, 53.5, 90),                      # BOOT = IO0 (pad 4, left top)
    ("R2", 127.5, 45.5, 0), ("C6", 127.5, 47.5, 0),                         # EN (pad 45, right top)
    ("J2", 131.5, 48.0, 0),                                                 # UART/USB/EN/BOOT jig pads
    ("R7", 128.0, 56.5, 0), ("D1", 128.0, 58.5, 0),                         # LED = IO38 (pad 34, right)
    ("C4", 106.0, 73.0, 90), ("C5", 104.0, 73.0, 90),                       # SX1262 VDD (pad 3, left)
    ("Q1", 102.0, 89.0, 0), ("R1", 102.0, 92.0, 0), ("C1", 102.0, 95.5, 0),
    ("R4", 129.0, 66.0, 90), ("R5", 131.0, 66.0, 90), ("C7", 133.0, 66.0, 90),
    ("Q2", 130.0, 71.0, 0), ("R6", 133.5, 71.0, 0),                         # battery sense
    ("TP1", 134.5, 92.0, 0),
]
for ref, x, y, rot in PREF:
    pack(ref, x, y, rot)

# Battery connector on the back, under the SX1262 (the cell sits behind the board in the cap)
place("J3", D.STRIP_CX, 70.0, 0)
fps["J3"].Flip(fps["J3"].GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)

# ---- strip routing (In2.Cu) ---------------------------------------------------------------
def track(a, b, layer, n, w=0.15):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pt(*a)); t.SetEnd(pt(*b)); t.SetLayer(layer); t.SetWidth(mm(w)); t.SetNet(n); t.SetLocked(True)
    board.Add(t)

def via(x, y, n):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y)); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(n)
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLocked(True)
    board.Add(v)

for i in range(1, 14):
    sense = net("/P%d" % i if i <= 12 else "/REF")
    k = lane_k(i)
    xl, xt = lane_x(k), rs_x(k)
    y_pad = D.pad_center_y(i) if i <= 12 else D.REF_CENTER_Y
    via(xl, y_pad, sense)                                        # pad -> In2
    dx = xt - xl
    y_bend = D.FAN_START_Y - abs(dx)
    track((xl, y_pad), (xl, D.FAN_START_Y), L["In2"], sense)       # straight up the strip
    if abs(dx) > 1e-6:
        track((xl, D.FAN_START_Y), (xt, y_bend), L["In2"], sense)  # 45 degree fan-out
    track((xt, y_bend), (xt, D.VIA_Y), L["In2"], sense)
    via(xt, D.VIA_Y, sense)                                      # In2 -> F.Cu
    rs_pad = [p for p in fps["RS%d" % i].Pads() if p.GetNumber() == "2"][0]
    rp = rs_pad.GetPosition()
    track((xt, D.VIA_Y), (pcbnew.ToMM(rp.x), pcbnew.ToMM(rp.y)), L["F"], sense)

# GND return pad stitched to the back plane
for dy in (-6, 0, 6):
    via(D.STRIP_CX, D.GND_PAD_CENTER_Y + dy, GND)

# ---- zones -----------------------------------------------------------------------------------
def zone(layer, poly, n=GND, hatch=False, prio=0, name=""):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(n); z.SetAssignedPriority(prio); z.SetZoneName(name)
    o = z.Outline(); o.NewOutline()
    for x, y in poly:
        o.Append(mm(x), mm(y))
    z.SetLocalClearance(mm(0.3)); z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
    if hatch:
        z.SetFillMode(pcbnew.ZONE_FILL_MODE_HATCH_PATTERN)
        z.SetHatchThickness(mm(0.2)); z.SetHatchGap(mm(1.3))   # ~25 % copper
        z.SetHatchOrientation(pcbnew.EDA_ANGLE(45, pcbnew.DEGREES_T))
    board.Add(z)

M = 1.0
whole = [(hx0 - M, hy0 - M), (hx1 + M, hy0 - M), (hx1 + M, hy1), (sx1 + M, sy0), (sx1 + M, sy1 + M),
         (sx0 - M, sy1 + M), (sx0 - M, sy0), (hx0 - M, hy1)]
head = [(hx0 - M, hy0 - M), (hx1 + M, hy0 - M), (hx1 + M, hy1 + 0.5), (hx0 - M, hy1 + 0.5)]
strip = [(sx0 - M, hy1 + 0.5), (sx1 + M, hy1 + 0.5), (sx1 + M, sy1 + M), (sx0 - M, sy1 + M)]
zone(L["B"], whole, name="GND_back")
zone(L["In2"], whole, name="GND_in2")
zone(L["In1"], head, name="GND_in1_head")
zone(L["In1"], strip, hatch=True, prio=1, name="GND_in1_hatch")
zone(L["F"], head, name="GND_top_head")

# ---- silkscreen / notes ------------------------------------------------------------------------
def text(s, x, y, layer, size=1.0, rot=0, mirror=False, just=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(pt(x, y)); t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(size * 0.15))
    t.SetTextAngleDegrees(rot); t.SetMirrored(mirror)
    if just == "left":
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    board.Add(t)

def silk(a, b, layer, w=0.15):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pt(*a)); s.SetEnd(pt(*b)); s.SetLayer(layer); s.SetWidth(mm(w)); board.Add(s)

# Back-side centimetre scale (cm relative to soil) on both strip edges
for cm in range(8, -21, -1):
    y = D.SOIL_Y - cm * 10
    if y < sy0 + 2 or y > sy1 - 6:
        continue
    ln = 2.0 if cm % 5 == 0 else 1.0
    silk((sx0 + 0.4, y), (sx0 + 0.4 + ln, y), pcbnew.B_SilkS)
    silk((sx1 - 0.4 - ln, y), (sx1 - 0.4, y), pcbnew.B_SilkS)
    if cm % 5 == 0 and cm != 0:
        text("%+d" % cm, D.STRIP_CX, y, pcbnew.B_SilkS, 1.2, mirror=True)
silk((sx0 + 0.4, D.SOIL_Y), (sx1 - 0.4, D.SOIL_Y), pcbnew.B_SilkS, 0.3)
silk((sx0 + 0.4, D.SOIL_Y), (sx1 - 0.4, D.SOIL_Y), pcbnew.F_SilkS, 0.3)
text("SOIL", D.STRIP_CX, D.SOIL_Y - 2.0, pcbnew.B_SilkS, 1.2, mirror=True)
text("SOIL", D.STRIP_CX, D.SOIL_Y - 2.0, pcbnew.F_SilkS, 1.2)
text("AWD node v0.1", D.STRIP_CX, hy1 - 3.0 - 30, pcbnew.B_SilkS, 1.2, mirror=True)
text("Agri Sensors and Controls", D.STRIP_CX, hy1 - 1.5 - 30, pcbnew.B_SilkS, 1.0, mirror=True)

notes = [
    "FAB: 4 layers, 1.6 mm FR4, ENIG, green mask both sides, vias tented.",
    "Stack: F.Cu pads (no mask opening) / In1 GND (hatched under pads) / In2 sense traces + GND / B.Cu solid GND.",
    "Strip below the neck: NO exposed copper. After assembly dip-coat strip in 2-part epoxy or PU (100-200 um), seal routed edges.",
    "Head lives in a sealed 50 mm PVC end cap; SMA -> pigtail -> 866 MHz antenna on a 1.5 m pole.",
    "Pads: P1 = +7..+5 cm ... P12 = -15..-17 cm; pump OFF when P2 wet (+5 cm), ON when P11 dry (-15 cm).",
    "Head routing (RS -> ESP32 touch pins, LoRa SPI, power) is still to be done by hand.",
]
for n, s in enumerate(notes):
    text(s, 145, 45 + n * 3, pcbnew.Dwgs_User, 1.5, just="left")

# ---- fill + save -----------------------------------------------------------------------------
pcbnew.SaveBoard(OUTFILE, board)
board = pcbnew.LoadBoard(OUTFILE)          # the filler crashes on a board that was only built in memory
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
pcbnew.SaveBoard(OUTFILE, board)
print("board written:", OUTFILE, "footprints:", len(fps), "nets:", len(nets))

# SaveBoard rewrites the .kicad_pro with defaults - put our net classes and rules back
import shutil
shutil.copy(os.path.join(HERE, "project_template.kicad_pro"), os.path.join(PRJ, D.PROJECT + ".kicad_pro"))
