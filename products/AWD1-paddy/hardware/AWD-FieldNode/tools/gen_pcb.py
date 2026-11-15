"""Build AWD-FieldNode.kicad_pcb (v0.3, 2 layers) from netlist.json with KiCad's pcbnew module.

Run with KiCad's bundled python, then tools/route.py (Freerouting) and tools/stitch.py. What it does:
  - outline: 40 x 60 mm head + 16 x 300 mm strip
  - places every footprint (head parts by a nearest-free-spot packer, enclosure-fixed parts by hand)
  - draws the whole strip on F.Cu, locked:
        pads in the left column; lanes in a "staircase" on the right, each between two GND guards.
        Lane c runs straight down and steps left into its pad; its left guard ends just above that
        pad in a via to the back GND. At the neck all traces fan out 0.3 -> 0.8 mm to the RS row.
  - zones: B.Cu GND (hatched behind the pads, solid elsewhere), F.Cu GND in the head
  - back silkscreen: pad names, a cm scale and the SOIL line
Head routing (RS -> ESP32 touch pins, LoRa SPI, power, charger) is done by route.py.
"""
import json, os, shutil
import pcbnew
from pcbnew import FromMM as mm, VECTOR2I
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
FPROOT = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints/"
OUTFILE = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")

board = pcbnew.BOARD()
board.SetCopperLayerCount(2)
ds = board.GetDesignSettings()
ds.SetBoardThickness(mm(1.6))
ds.m_CopperEdgeClearance = mm(0.3)
FCU, BCU = pcbnew.F_Cu, pcbnew.B_Cu

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

def place(ref, x, y, rot=0, lock=True):
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pt(x, y))
    fp.SetLocked(lock)

# Sensing pads (left column), REF on top, GND return pad across the strip at the bottom
for i in range(1, 13):
    place("E%d" % i, D.PAD_CX, D.pad_center_y(i))
place("E13", D.PAD_CX, D.REF_CENTER_Y)
place("E14", D.STRIP_CX, D.GND_PAD_CENTER_Y)

# Big parts: ESP32 module with its antenna at the top edge, SX1262 with ANT facing the SMA
place("U1", D.STRIP_CX, hy0 + 10.9)               # module top 0.65 mm inside the edge, so its silk stays on the board
place("U2", D.STRIP_CX, 72.0)
# Edge-mount SMA: in the footprint the body face is 2.54 mm from the origin, where the pads end. v0.1 had it
# at hx0 + 1.1, i.e. the pads ran 1.4 mm past the edge. v0.3: pads end 0.3 mm inside the edge.
place("J1", hx0 + 2.54 + 0.3, 65.0, 180)
# Enclosure-fixed: PAIR button and status LED sit under the front-shell features (awd_enclosure.scad)
place("SW1", 102.5, 53.5, 90)
place("R7", 128.0, 56.5, 0)
place("D1", 128.0, 58.5, 0)

# ---- strip traces: index t = 0..26, even = guard G(t/2), odd = lane c = (t-1)/2 ----------------
# lane 0 = REF, lanes 1..12 = P1..P12 (REF is the top pad, so its lane is the shortest)
def x_b(t):
    return D.BUNDLE_X0 + t * D.TRACE_PITCH
def x_f(t):
    return D.FAN_CX + (t - 13) * D.FAN_PITCH
def pad_top(c):
    return D.REF_CENTER_Y - D.REF_H / 2 if c == 0 else D.pad_center_y(c) - D.PAD_H / 2
def lane_net(c):
    return net("/REF" if c == 0 else "/P%d" % c)
def rs_ref(c):
    return "RS13" if c == 0 else "RS%d" % c

# RS row: one 510R per lane, vertical, pad 1 (touch pin side) up, pad 2 (lane side) down
for c in range(13):
    ref = rs_ref(c)
    place(ref, x_f(2 * c + 1), D.RS_Y, 90)
    p1, p2 = sorted(fps[ref].Pads(), key=lambda p: p.GetNumber())
    if p2.GetPosition().y < p1.GetPosition().y:
        place(ref, x_f(2 * c + 1), D.RS_Y, 270)
    fps[ref].Reference().SetVisible(False)

def track(pts, n, w=0.15, layer=FCU):
    for a, b in zip(pts, pts[1:]):
        if abs(a[0] - b[0]) < 1e-6 and abs(a[1] - b[1]) < 1e-6:
            continue
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pt(*a)); t.SetEnd(pt(*b)); t.SetLayer(layer); t.SetWidth(mm(w)); t.SetNet(n)
        t.SetLocked(True)
        board.Add(t)

def via(x, y, n, d=0.6, drill=0.3):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y)); v.SetWidth(mm(d)); v.SetDrill(mm(drill)); v.SetNet(n)
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLocked(True)
    board.Add(v)

def fan(t):
    """Neck part of trace t: (end of the 45 deg run at fan pitch, start of it at bundle pitch)."""
    k = abs(t - 13)
    y_s = D.FAN_TOP_Y + D.FAN_STAGGER * k              # outer traces bend lower, so the 45 deg runs stay apart
    dx = x_f(t) - x_b(t)
    return (x_f(t), y_s - abs(dx)), (x_b(t), y_s)

for t in range(27):
    fe, fs = fan(t)
    if t % 2:                                           # lane
        c = (t - 1) // 2
        rs2 = [p for p in fps[rs_ref(c)].Pads() if p.GetNumber() == "2"][0].GetPosition()
        y_jog = pad_top(c) + 1.0
        track([(pcbnew.ToMM(rs2.x), pcbnew.ToMM(rs2.y)), fe, fs, (x_b(t), y_jog), (D.PAD_CX + 2.0, y_jog)],
              lane_net(c))
    else:                                               # guard
        g = t // 2
        y_g = pad_top(g) - 1.0 if g < 13 else pad_top(12) + D.PAD_H + 2.0   # outermost guard runs past P12
        end = (x_b(t) - 0.6, y_g + 0.6)
        track([(x_f(t), D.GUARD_VIA_Y), fe, fs, (x_b(t), y_g), end], GND)
        via(x_f(t), D.GUARD_VIA_Y, GND)
        via(end[0], end[1], GND)

# ---- touch bus in the head (pre-drawn, locked) --------------------------------------------------
# Each U1 touch pin -> short F.Cu stub -> via at BUS_VIA_X -> B.Cu: across, then straight down under
# the LoRa module (F.Cu above is a GND pour) -> via -> F.Cu step right -> down into its RS pad 1.
# Lane j (0 = REF, 1..12 = P1..P12) is driven by T(13 - j), so the ladder never crosses itself.
def u1pad(netname):
    return [p for p in fps["U1"].Pads() if p.GetNetname() == netname][0]
for j in range(13):
    ch = 13 - j
    n = net("/T%d" % ch)
    pad = u1pad("/T%d" % ch)
    px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    xv = D.BUS_X0 + D.BUS_PITCH * j
    yb = D.BUS_Y0 - D.BUS_STEP * j
    if ch <= 11:                                     # pins on U1's left edge
        yin = py
        track([(px, py), (D.BUS_VIA_X, py)], n, D.BUS_W)
    else:                                            # T12 / T13 on U1's bottom edge: down, then left
        yin = py + (1.15 if ch == 12 else 2.15)
        track([(px, py), (px, yin), (D.BUS_VIA_X, yin)], n, D.BUS_W)
    via(D.BUS_VIA_X, yin, n)
    track([(D.BUS_VIA_X, yin), (xv, yin), (xv, yb)], n, D.BUS_W, BCU)
    via(xv, yb, n)
    rs1 = [p for p in fps[rs_ref(j)].Pads() if p.GetNumber() == "1"][0].GetPosition()
    xr, yr = pcbnew.ToMM(rs1.x), pcbnew.ToMM(rs1.y)
    track([(xv, yb), (xr, yb), (xr, yr)], n, D.BUS_W)

# GND return pad stitched to the back plane
for dx in (-3.5, 0, 3.5):
    for dy in (-6, 0, 6):
        via(D.STRIP_CX + dx, D.GND_PAD_CENTER_Y + dy, GND)

# ---- everything else in the head: nearest free spot to a preferred location --------------------
def bbox(fp):
    c = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
    b = c.BBox() if c.OutlineCount() else fp.GetBoundingBox(False)
    return [pcbnew.ToMM(v) for v in (b.GetX(), b.GetY(), b.GetRight(), b.GetBottom())]

blocked = [
    (hx0, hy0, hx1, hy0 + 5.25),                                    # ESP32 antenna keepout band
    (D.STRIP_CX - 7.9, hy0, D.STRIP_CX + 7.9, hy0 + 21.4),         # ESP32 body
    (D.STRIP_CX - 9.4, 72 - 9.1, D.STRIP_CX + 9.4, 72 + 9.1),       # SX1262 module
    (hx0, 59.0, bbox(fps["J1"])[2], 71.0),                          # SMA
    (x_f(0) - 0.6, D.RS_Y - 1.2, x_f(26) + 0.6, hy1),              # RS row, guard vias and fan-out
    (D.BUS_VIA_X - 0.6, 50.0, D.BUS_VIA_X + 0.6, 63.5),            # touch-bus via column
    (D.BUS_X0 - 0.6, D.BUS_Y0 - D.BUS_STEP * 12 - 0.6, x_f(26) + 0.6, D.RS_Y),   # touch-bus F.Cu steps
] + [bbox(fps[r]) for r in ("SW1", "R7", "D1")]

blocked_b = [
    (hx0, hy0, hx1, hy0 + 6.0),                                     # under the ESP32 antenna
    (x_f(0) - 0.8, D.GUARD_VIA_Y - 0.8, x_f(26) + 0.8, D.GUARD_VIA_Y + 0.8),   # guard via row
    (D.BUS_VIA_X - 0.6, 50.0, D.BUS_X0 + D.BUS_PITCH * 12 + 0.6, D.BUS_Y0 + 0.6),   # touch-bus ladder on B.Cu
]

def free(b, side="F", m=0.25):
    if b[0] < hx0 + 0.5 or b[2] > hx1 - 0.5 or b[1] < hy0 + 0.5 or b[3] > hy1 - 0.5:
        return False
    obs = blocked if side == "F" else blocked_b
    return not any(b[0] - m < o[2] and b[2] + m > o[0] and b[1] - m < o[3] and b[3] + m > o[1] for o in obs)

def pack(ref, px, py, rot=0, back=False):
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pt(px, py))
    if back:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    b0 = bbox(fp)
    side = "B" if back else "F"
    best = None
    for gx in range(-80, 81):
        for gy in range(-120, 121):
            dx, dy = gx * 0.25, gy * 0.25
            d = dx * dx + dy * dy
            if best and d >= best[0]:
                continue
            b = [b0[0] + dx, b0[1] + dy, b0[2] + dx, b0[3] + dy]
            if free(b, side):
                best = (d, dx, dy, b)
    if not best:
        raise RuntimeError("no room for " + ref)
    _, dx, dy, b = best
    fp.SetPosition(pt(px + dx, py + dy))
    (blocked if side == "F" else blocked_b).append(b)
    print("  %-4s %s at (%.2f, %.2f) moved %.1f mm" % (ref, side, px + dx, py + dy, best[0] ** 0.5))

PREF = [  # ref, preferred x, y, rotation - grouped by block, next to the pins they serve
    # ESP32 support (top corners beside the module)
    ("C3", 107.8, 47.0, 90), ("C2", 106.0, 47.0, 90), ("R3", 104.2, 47.0, 90),
    ("R2", 127.5, 45.5, 0), ("C6", 127.5, 47.3, 0), ("J2", 132.0, 49.0, 0),
    # charge LEDs next to the status LED (front-shell light window)
    ("D2", 131.0, 58.5, 0), ("D3", 134.0, 58.5, 0), ("R10", 131.0, 56.5, 0), ("R11", 134.0, 56.5, 0),
    # LoRa decoupling
    ("C4", 106.0, 74.0, 90), ("C5", 104.0, 74.0, 90),
    # battery sense (right column)
    ("R4", 129.0, 66.0, 90), ("R5", 131.0, 66.0, 90), ("C7", 133.0, 66.0, 90),
    ("Q2", 130.0, 70.5, 0), ("R6", 133.5, 70.5, 0),
    # charger detect divider + test point (right, low)
    ("R12", 135.5, 74.5, 90), ("R13", 135.5, 77.5, 270), ("TP1", 135.5, 97.0, 0),
]
for ref, x, y, rot in PREF:
    pack(ref, x, y, rot)

# ---- back of the head = power: battery and magnetic-socket connectors, charger, LDO, protection ----
# (front = ESP32 + LoRa + touch traces; the 13 touch traces need the front-left corner to themselves)
BACK = [  # ref, preferred x, y, rotation
    ("J3", 131.5, 66.0, 90),                # battery (the cell sits behind the board); kept off the touch bus under U2
    ("J4", 131.5, 85.0, 90),                # magnetic-socket cable
    ("U5", 128.0, 76.0, 0),                 # USB ESD, between J4 and the ESP32's USB pins
    ("F1", 124.0, 84.0, 90), ("C8", 121.5, 84.0, 90),
    ("U3", 116.0, 84.5, 0), ("R8", 111.5, 82.5, 90), ("R9", 113.0, 82.5, 90),
    ("U4", 106.0, 84.0, 0), ("C9", 103.5, 84.0, 90), ("C1", 106.0, 88.0, 0),
    ("Q1", 103.0, 78.0, 0), ("R1", 103.0, 81.0, 0),
]
for ref, x, y, rot in BACK:
    pack(ref, x, y, rot, back=True)

# J2's GND pin: solid to the pour (a thermal there only gets one spoke between the 1.27 mm pins)
for pad in fps["J2"].Pads():
    if pad.GetNetname() == "GND":
        pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)

# ---- zones -----------------------------------------------------------------------------------
def zone(layer, poly, n=GND, hatch=False, prio=0, name=""):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(n); z.SetAssignedPriority(prio); z.SetZoneName(name)
    o = z.Outline(); o.NewOutline()
    for x, y in poly:
        o.Append(mm(x), mm(y))
    z.SetLocalClearance(mm(0.25)); z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
    if hatch:
        z.SetFillMode(pcbnew.ZONE_FILL_MODE_HATCH_PATTERN)
        z.SetHatchThickness(mm(0.2)); z.SetHatchGap(mm(1.3))     # ~25 % copper behind the pads
        z.SetHatchOrientation(pcbnew.EDA_ANGLE(45, pcbnew.DEGREES_T))
    board.Add(z)

M = 1.0
XS = D.BUNDLE_X0 - 0.4                         # hatched (pads) / solid (lanes) split on B.Cu
Y_HB = pad_top(12) + D.PAD_H + 4.0             # below P12: solid across the strip
solid = [(hx0 - M, hy0 - M), (hx1 + M, hy0 - M), (hx1 + M, hy1 + 0.5), (sx1 + M, hy1 + 0.5), (sx1 + M, sy1 + M),
         (sx0 - M, sy1 + M), (sx0 - M, Y_HB), (XS, Y_HB), (XS, hy1 + 0.5), (hx0 - M, hy1 + 0.5)]
hatch = [(sx0 - M, hy1 + 0.5), (XS, hy1 + 0.5), (XS, Y_HB), (sx0 - M, Y_HB)]
zone(BCU, solid, name="GND_back")
zone(BCU, hatch, hatch=True, prio=1, name="GND_back_hatch")
zone(FCU, [(hx0 - M, hy0 - M), (hx1 + M, hy0 - M), (hx1 + M, hy1 + 0.5), (hx0 - M, hy1 + 0.5)], name="GND_top_head")

# No routing on the back of the strip: B.Cu there is the GND shield (vias are allowed: the guard vias)
ka = pcbnew.ZONE(board)
ka.SetIsRuleArea(True); ka.SetLayer(BCU); ka.SetZoneName("strip_back_no_tracks")
ka.SetDoNotAllowTracks(True); ka.SetDoNotAllowVias(False); ka.SetDoNotAllowPads(False)
ka.SetDoNotAllowFootprints(False); ka.SetDoNotAllowCopperPour(False)
o = ka.Outline(); o.NewOutline()
for x, y in [(sx0 - M, hy1 + 0.6), (sx1 + M, hy1 + 0.6), (sx1 + M, sy1 + M), (sx0 - M, sy1 + M)]:
    o.Append(mm(x), mm(y))
board.Add(ka)

# No tracks or vias within 0.6 mm of the head edges (Freerouting ignores KiCad's copper-to-edge rule)
def keepout(name, x0, y0, x1, y1, vias=False, front_only=False):
    k = pcbnew.ZONE(board)
    k.SetIsRuleArea(True); k.SetZoneName(name)
    if front_only:
        k.SetLayer(FCU)
    else:
        k.SetLayerSet(pcbnew.LSET.AllCuMask())
    k.SetDoNotAllowTracks(True); k.SetDoNotAllowVias(not vias); k.SetDoNotAllowPads(False)
    k.SetDoNotAllowFootprints(False); k.SetDoNotAllowCopperPour(False)
    o = k.Outline(); o.NewOutline()
    for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        o.Append(mm(x), mm(y))
    board.Add(k)
E = 0.6
keepout("edge_left", hx0 - 1, hy0 - 1, hx0 + E, hy1 + 1)
keepout("edge_right", hx1 - E, hy0 - 1, hx1 + 1, hy1 + 1)
keepout("edge_bottom_left", hx0 - 1, hy1 - E, sx0, hy1 + 1)
keepout("edge_bottom_right", sx1, hy1 - E, hx1 + 1, hy1 + 1)
# No F.Cu tracks under the LoRa module (between its two pad rows): F.Cu there fills as solid GND right
# under the radio, and traces that must pass (the touch bus) go on B.Cu, shielded by that pour.
keepout("under_lora_top", D.STRIP_CX - 6.6, 72 - 8.2, D.STRIP_CX + 6.6, 72 + 8.2, vias=True, front_only=True)

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

BS = pcbnew.B_SilkS
# Back: cm scale on the right edge, pad names behind each pad (mirrored, reads correctly from the back)
for cm in range(8, -21, -1):
    y = D.SOIL_Y - cm * 10
    if y < sy0 + 2 or y > sy1 - 10:
        continue
    ln = 2.0 if cm % 5 == 0 else 1.0
    silk((sx1 - 0.5 - ln, y), (sx1 - 0.5, y), BS)
    if cm % 5 == 0 and cm != 0:
        text("%+dcm" % cm, sx1 - 6.5, y, BS, 1.0, mirror=True)
for i in range(1, 13):
    y = D.pad_center_y(i)
    text("P%d" % i, D.PAD_CX, y - 4.0 if abs(y - D.SOIL_Y) < 3 else y, BS, 1.5, mirror=True)   # P4 dodges SOIL
text("REF", D.PAD_CX, D.REF_CENTER_Y, BS, 1.3, mirror=True)
text("GND", D.STRIP_CX, D.GND_PAD_CENTER_Y, BS, 1.5, mirror=True)
for layer in (BS, pcbnew.F_SilkS):
    silk((sx0 + 0.4, D.SOIL_Y), (sx1 - 0.4, D.SOIL_Y), layer, 0.3)
text("SOIL", D.PAD_CX, D.SOIL_Y + 2.0, BS, 1.2, mirror=True)
text("AWD node v0.3", D.STRIP_CX, 46.0, BS, 1.4, mirror=True)
text("Agri Sensors and Controls", D.STRIP_CX, 48.5, BS, 1.0, mirror=True)

notes = [
    "FAB: 2 layers, 1.6 mm FR4, HASL lead-free (pads stay under mask), green mask both sides, vias tented.",
    "Top: sensing pads (no mask opening) + staircase of 0.15 mm lanes, each between two GND guards (0.15 mm gaps).",
    "Back: GND - 25 % hatch behind the pads, solid behind the lanes and in the head.",
    "Strip below the neck: NO exposed copper. After assembly dip-coat strip in 2-part epoxy or PU (100-200 um), seal routed edges.",
    "Head lives in a sealed 50 mm PVC end cap; SMA -> pigtail -> 866 MHz antenna on a 1.5 m pole.",
    "Pads: P1 = +7..+5 cm ... P12 = -15..-17 cm; pump OFF when P2 wet (+5 cm), ON when P11 dry (-15 cm).",
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
shutil.copy(os.path.join(HERE, "project_template.kicad_pro"), os.path.join(PRJ, D.PROJECT + ".kicad_pro"))
