"""Build TestJig.kicad_pcb (placement, nets, outline, pours, silkscreen) with KiCad's bundled Python.

Run:  <KiCad python3> tools/gen_pcb.py
Local coordinates are mm from the board's top-left corner; outline and holes follow WM1 (see board.py).
"""
import math
import os
import re
import sys
import uuid

import wx  # noqa: E402
_APP = wx.App(False)
import pcbnew  # noqa: E402

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
from board import OX, OY, W, H, R_CORNER, HOLES, ANTENNA_KO, inside, rect_inside  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(ROOT, 'TestJig.kicad_pcb')
FPDIR = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints'
TXT = 0.8                                   # silkscreen text height for refs / values / marks


def U(seed):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, 'testjig/' + seed))


def V(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + x), pcbnew.FromMM(OY + y))


# ===================================================================== placement
# Box layout (agreed 2026-10-21): inputs on the LEFT wall, outputs on the RIGHT wall, DevKit USB out of the
# right wall through a gap in the output connectors, LoRa SMA through the top wall, holes on the centre line.
# ref: (x, y, rotation_deg, side)
PLACE = {'H%d' % (i + 1): (x, y, 0, 'F') for i, (x, y) in enumerate(HOLES)}

XL, XR = 4.5, 94.5                          # pin columns of the left / right edge connectors (rot 270: pin 1 on top)
LEFT = [('J1', 4.0), ('J2', 20.0), ('J3', 36.0), ('J4', 52.0), ('J9', 68.0), ('J11', 81.5)]
RIGHT = [('J5', 4.0), ('J6', 20.0), ('J7', 48.0), ('J8', 64.0), ('J10', 80.0), ('J14', 91.0)]
USB_GAP = (33.0, 45.0)                      # no connectors here: the DevKit's USB cable leaves the right wall
for ref, y in LEFT:
    PLACE[ref] = (XL, y, 270, 'F')
for ref, y in RIGHT:
    PLACE[ref] = (XR, y, 270, 'F')

for k, (ref, y1) in enumerate(LEFT[:4]):    # relay sense: pin -> 1k -> 10k, one row per pin
    for i in range(4):
        PLACE['R%d' % (101 + 4 * k + i)] = (12.45, y1 + 2.5 * (i + 1), 0, 'F')
        PLACE['R%d' % (121 + 4 * k + i)] = (16.6, y1 + 2.5 * (i + 1), 0, 'F')
for k, (ref, y1) in enumerate(RIGHT[:4]):   # DUT drive: 330R in line with each pin
    for i in range(4):
        PLACE['R%d' % (141 + 4 * k + i)] = (86.6, y1 + 2.5 * (i + 1), 0, 'F')   # pad 1 (DO) faces the 595, pad 2 the pin
PLACE.update({
    # pulse in (J9) and out (J14), in line with their pins
    'R6': (12.45, 68.0, 0, 'F'), 'R8': (16.6, 68.0, 0, 'F'), 'R7': (12.45, 70.5, 0, 'F'), 'R9': (16.6, 70.5, 0, 'F'),
    'R4': (86.6, 91.0, 0, 'F'), 'R5': (86.6, 93.5, 0, 'F'),
    # AI1 / AI2 dividers next to J11
    'R14': (12.5, 81.5, 0, 'F'), 'R15': (16.8, 81.5, 0, 'F'), 'C18': (21.1, 81.5, 0, 'F'),
    'R16': (12.5, 87.0, 0, 'F'), 'R17': (16.8, 87.0, 0, 'F'), 'C19': (21.1, 87.0, 0, 'F'),
    # 74HC165 chain behind the inputs, 74HC595 chain behind the outputs
    'U5': (22.5, 17.0, 0, 'F'), 'C6': (22.5, 24.6, 90, 'F'),
    'U6': (22.5, 49.0, 0, 'F'), 'C7': (22.5, 56.6, 90, 'F'),
    'U2': (78.5, 17.0, 0, 'F'), 'C3': (78.5, 9.0, 0, 'F'),
    'U3': (78.5, 62.0, 0, 'F'), 'C4': (78.5, 70.5, 0, 'F'),
    # ESP32 DevKit: USB end on the right (x 82), antenna end on the left; rows at y 26.3 / 51.7
    'U1': (76.2, 26.3, 90, 'F'),
    'C1': (79.5, 28.0, 90, 'F'), 'C2': (79.5, 55.0, 0, 'F'),
    'R1': (52.0, 56.0, 0, 'F'), 'R2': (60.0, 56.0, 0, 'F'),
    # LoRa at the top, SMA straight up through the top wall
    'U10': (36.5, 14.0, 0, 'F'), 'J13': (28.5, 1.6, 90, 'F'),
    'C9': (50.5, 12.0, 90, 'F'), 'R3': (50.5, 18.0, 90, 'F'),
    # status LEDs + driver, top right
    'U4': (63.75, 20.8, 90, 'F'), 'C5': (72.5, 20.8, 90, 'F'),
    # jig inputs, bottom middle; J12 on the bottom edge
    'SW1': (31.0, 60.0, 0, 'F'), 'U7': (45.0, 64.0, 0, 'F'), 'C8': (45.0, 71.0, 0, 'F'),
    'J12': (57.5, 95.0, 0, 'F'),
    # 4-20 mA self-test jumpers + loads
    'JP1': (55.0, 61.0, 0, 'F'), 'R27': (59.2, 62.27, 0, 'F'),
    'JP2': (63.6, 61.0, 0, 'F'), 'R37': (67.8, 62.27, 0, 'F'),
    # AO readback dividers
    'R10': (54.0, 71.0, 0, 'F'), 'R11': (58.0, 71.0, 0, 'F'), 'C16': (62.0, 71.0, 0, 'F'),
    'R12': (66.0, 71.0, 0, 'F'), 'R13': (70.0, 71.0, 0, 'F'), 'C17': (74.0, 71.0, 0, 'F'),
    'U11': (62.0, 80.0, 0, 'F'),
})
for i in range(8):
    PLACE['D%d' % (7 + i)] = (55.0 + 2.5 * i, 5.8, 270, 'F')      # anode down, facing its 470R
    PLACE['R%d' % (161 + i)] = (55.0 + 2.5 * i, 12.8, 90, 'F')
    PLACE['R%d' % (171 + i)] = (30.5 + 2.5 * i, 74.5, 90, 'F')


def ao_place(x0, fy, U_, Qn, Qp, D, rb, cb, Cd):
    """One 4-20 mA channel: op-amp/transistor stages under the plugged-in DevKit (three rows between its pin
    rows, low parts only); the PWM RC filter (R, C, R, C) in a row below the DevKit at y = fy."""
    PLACE.update({
        U_: (x0 + 4.5, 32.0, 0, 'F'), Qn: (x0 + 12.0, 32.0, 0, 'F'), 'R%d' % (rb + 2): (x0 + 17.0, 32.0, 0, 'F'),
        'R%d' % (rb + 3): (x0 + 2.0, 39.0, 0, 'F'), 'R%d' % (rb + 4): (x0 + 7.0, 39.0, 0, 'F'),
        'R%d' % (rb + 5): (x0 + 12.0, 39.0, 0, 'F'), 'R%d' % (rb + 6): (x0 + 17.0, 39.0, 0, 'F'),
        Qp: (x0 + 2.5, 46.0, 0, 'F'), D: (x0 + 8.0, 46.0, 0, 'F'), Cd: (x0 + 14.0, 46.0, 0, 'F'),
        'R%d' % rb: (52.0, fy, 0, 'F'), 'C%d' % cb: (56.5, fy, 0, 'F'),
        'R%d' % (rb + 1): (61.0, fy, 0, 'F'), 'C%d' % (cb + 1): (65.5, fy, 0, 'F'),
    })


ao_place(41.5, 77.0, 'U8', 'Q1', 'Q2', 'D1', 20, 12, 'C10')
ao_place(60.5, 82.5, 'U9', 'Q3', 'Q4', 'D2', 30, 14, 'C11')
PLACE['C20'] = (79.5, 46.0, 90, 'F')
PLACE['C1'] = (81.2, 28.0, 90, 'F')
PLACE['U11'] = (71.5, 80.0, 0, 'F')

# ===================================================================== fixed silkscreen
SILK = [  # (text, x, y, size, angle)
    ('470E: R161-168', 63.75, 15.85, TXT, 0),
    ('10K: R171-178', 39.25, 78.0, TXT, 0),
    ('SW1 DIP-4: product / mode', 35.2, 55.9, TXT, 0),
    ('JP1/JP2: SELF-TEST', 61.4, 66.6, TXT, 0),
    ('1K: R6,R7', 14.0, 77.0, TXT, 0), ('10K: R8,R9', 14.0, 78.1, TXT, 0),
    ('330E R4,R5', 86.0, 88.4, TXT, 0),
    ('USB', 88.2, 39.0, 1.0, 0),
    ('J13 LoRa ANT', 37.5, 1.3, TXT, 0),
    ('TestJig-ESP32 rev D', 80.5, 97.9, TXT, 0),
]
for _i, _t in enumerate(['P', 'F', 'B', 'L', '1', '2', '3', '4']):
    SILK.append((_t, 55.0 + 2.5 * _i, 2.3, TXT, 0))

# Port labels: a label next to every pin, with the firmware channel number (IN1..16 left, OUT1..16 right).
ARROWS = []   # (x0, y0, x1, y1): filled arrow on F.SilkS from (x0, y0) to the tip at (x1, y1)
PINL = {'J1': ['G', '1', '2', '3', '4'], 'J2': ['G', '5', '6', '7', '8'], 'J3': ['G', '9', '10', '11', '12'],
        'J4': ['G', '13', '14', '15', '16'], 'J9': ['I1', 'I2', 'G', 'V'], 'J11': ['A1', 'A2', 'G']}
PINR = {'J5': ['G', '1', '2', '3', '4'], 'J6': ['G', '5', '6', '7', '8'], 'J7': ['G', '9', '10', '11', '12'],
        'J8': ['G', '13', '14', '15', '16'], 'J10': ['A1', 'A2', 'G'], 'J14': ['O1', 'O2', 'G']}
for ref, y1 in LEFT:
    for i, t in enumerate(PINL[ref]):
        SILK.append((t, 9.25, y1 + 2.5 * i, TXT, 0))
for ref, y1 in RIGHT:
    for i, t in enumerate(PINR[ref]):
        SILK.append((t, 89.75, y1 + 2.5 * i, TXT, 0))
for k, (ref, y1) in enumerate(LEFT[:4]):
    n = 4 * k
    SILK += [('J%d IN %d-%d' % (k + 1, n + 1, n + 4), 14.0, y1 + 11.7, TXT, 0),
             ('1K: R%d-%d' % (101 + n, 104 + n), 14.0, y1 + 12.8, TXT, 0),
             ('10K: R%d-%d' % (121 + n, 124 + n), 14.0, y1 + 13.9, TXT, 0)]
    ARROWS.append((10.6, y1, 16.0, y1))     # into the jig
for k, (ref, y1) in enumerate(RIGHT[:4]):
    n = 4 * k
    ly = y1 + 11.7
    SILK += [('J%d OUT %d-%d' % (k + 5, n + 1, n + 4), 86.0, ly, TXT, 0),
             ('330E', 86.0, ly + 1.1, TXT, 0), ('R%d-%d' % (141 + n, 144 + n), 86.0, ly + 2.2, TXT, 0)]
    ARROWS.append((83.0, y1, 88.4, y1))     # out of the jig
SILK += [('J11 AIN', 14.0, 79.2, TXT, 0), ('J12 CTRL', 65.0, 89.9, TXT, 0)]
for _i, _t in enumerate(['ST', 'LD', 'S1', 'S2', 'G', 'V']):   # J12 START, LID, SPARE1, SPARE2, GND, 3V3
    SILK.append((_t, 57.5 + 2.5 * _i, 91.2, TXT, 0))


def add_arrow(board, x0, y0, x1, y1):
    """Filled arrow from (x0, y0) to the tip at (x1, y1) (horizontal or vertical): 1.2 mm shaft, 3 mm head."""
    L = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / L, (y1 - y0) / L
    px, py = -uy, ux
    hx, hy = x1 - 2.0 * ux, y1 - 2.0 * uy
    pts = [(x0 + 0.6 * px, y0 + 0.6 * py), (hx + 0.6 * px, hy + 0.6 * py), (hx + 1.5 * px, hy + 1.5 * py), (x1, y1),
           (hx - 1.5 * px, hy - 1.5 * py), (hx - 0.6 * px, hy - 0.6 * py), (x0 - 0.6 * px, y0 - 0.6 * py)]
    sh = pcbnew.PCB_SHAPE(board)
    sh.SetShape(pcbnew.SHAPE_T_POLY)
    sh.SetPolyPoints([V(px_, py_) for px_, py_ in pts])
    sh.SetFilled(True)
    sh.SetLayer(pcbnew.F_SilkS)
    sh.SetWidth(pcbnew.FromMM(0.1))
    board.Add(sh)


def add_text(board, t, x, y, size, ang=0):
    tx = pcbnew.PCB_TEXT(board)
    tx.SetText(t)
    tx.SetLayer(pcbnew.F_SilkS)
    tx.SetPosition(V(x, y))
    tx.SetTextAngleDegrees(ang)
    tx.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(size), pcbnew.FromMM(size)))
    tx.SetTextThickness(pcbnew.FromMM(max(0.12, size * 0.18)))
    board.Add(tx)
    return tx


# ===================================================================== zones
def _pts(rect):
    x0, y0, x1, y1 = rect
    return ' '.join('(xy %.3f %.3f)' % (OX + x, OY + y) for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def add_zones():
    """pcbnew's SaveBoard crashes headless with zones present, so pours are written as text.
    The pour rectangle is clipped by the (rounded) board outline when filled."""
    txt = open(PCB).read()
    gnd = re.search(r'\(net (\d+) "GND"\)', txt).group(1)
    zones = []
    for layer in ('F.Cu', 'B.Cu'):
        zones.append('''	(zone (net %s) (net_name "GND") (layer "%s") (uuid "%s") (name "GND pour %s")
		(hatch edge 0.5) (priority 0)
		(connect_pads thru_hole_only (clearance 0.3))
		(min_thickness 0.25) (filled_areas_thickness no)
		(fill yes (thermal_gap 0.4) (thermal_bridge_width 0.5) (island_removal_mode 0))
		(polygon (pts %s))
	)''' % (gnd, layer, U('zone' + layer), layer, _pts((0.3, 0.3, W - 0.3, H - 0.3))))
    zones.append('''	(zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (uuid "%s") (name "DevKit antenna keepout")
		(hatch edge 0.5)
		(connect_pads (clearance 0))
		(min_thickness 0.25) (filled_areas_thickness no)
		(keepout (tracks allowed) (vias allowed) (pads allowed) (copperpour not_allowed) (footprints allowed))
		(fill (thermal_gap 0.5) (thermal_bridge_width 0.5))
		(polygon (pts %s))
	)''' % (U('antenna-ko'), _pts(ANTENNA_KO)))
    txt = txt.rstrip()
    assert txt.endswith(')')
    txt = txt[:-1].rstrip() + '\n' + '\n'.join(zones) + '\n)\n'
    open(PCB, 'w').write(txt)


# ===================================================================== silkscreen placement
def _box(bb, grow=0.0):
    to = pcbnew.ToMM
    return (to(bb.GetLeft()) - grow, to(bb.GetTop()) - grow, to(bb.GetRight()) + grow, to(bb.GetBottom()) + grow)


def _crt(f):
    cy = f.GetCourtyard(pcbnew.F_CrtYd)
    return _box(cy.BBox() if cy.OutlineCount() else f.GetBoundingBox(False, False))


def add_marks(board):
    """'+' at every LED anode and '1' at pin 1 of every IC, just beyond the pad (outside the part body)."""
    to = pcbnew.ToMM
    for f in board.GetFootprints():
        ref = f.GetReference()
        pads = {p.GetNumber(): p for p in f.Pads()}
        if ref.startswith('D') and 'LED' in f.GetFPIDAsString():
            a, k, mark = pads['2'], pads['1'], '+'
        elif ref.startswith('U') and ref != 'U1':
            a, k, mark = pads['1'], pads['2'], '1'
        else:
            continue
        ax, ay = to(a.GetPosition().x) - OX, to(a.GetPosition().y) - OY
        kx, ky = to(k.GetPosition().x) - OX, to(k.GetPosition().y) - OY
        dx, dy = ax - kx, ay - ky
        L = math.hypot(dx, dy) or 1.0
        pb = _box(a.GetBoundingBox())
        reach = max(pb[2] - pb[0], pb[3] - pb[1]) / 2 + 0.75
        if ref == 'U10':
            add_text(board, mark, ax - 2.2, ay, 1.0)     # module: beside pin 1 (the SMA sits above it)
        elif ref.startswith('U'):
            # IC pin rows: put the '1' just past the end of the row
            add_text(board, mark, ax + dx / L * (reach + 0.2), ay + dy / L * (reach + 0.2), 1.0)
        else:
            add_text(board, mark, ax + dx / L * reach, ay + dy / L * reach, 1.0)


def place_fields(board, size=TXT):
    """Reference and value of every part on F.SilkS, outside all courtyards and clear of pads, other text,
    holes and the board edge, so both stay readable after assembly. Returns fields that had no room."""
    mm = pcbnew.FromMM
    obst = []
    fps = list(board.GetFootprints())
    for f in fps:
        if f.GetReference() != 'U1':          # the socketed DevKit's area is usable for text
            obst.append(_crt(f))
        for pad in f.Pads():
            obst.append(_box(pad.GetBoundingBox(), 0.15))
        for g in f.GraphicalItems():
            if g.GetLayer() != pcbnew.F_SilkS:
                continue
            if g.GetClass() == 'PCB_SHAPE' and g.GetShape() == pcbnew.SHAPE_T_RECT:
                b0 = _box(g.GetBoundingBox())   # outline rectangle: only its four edges are in the way
                obst += [(b0[0] - 0.2, b0[1] - 0.2, b0[2] + 0.2, b0[1] + 0.2), (b0[0] - 0.2, b0[3] - 0.2, b0[2] + 0.2, b0[3] + 0.2),
                         (b0[0] - 0.2, b0[1] - 0.2, b0[0] + 0.2, b0[3] + 0.2), (b0[2] - 0.2, b0[1] - 0.2, b0[2] + 0.2, b0[3] + 0.2)]
            else:
                obst.append(_box(g.GetBoundingBox(), 0.1))
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.F_SilkS:
            obst.append(_box(d.GetBoundingBox(), 0.1))

    def hit(r):
        loc = (r[0] - OX, r[1] - OY, r[2] - OX, r[3] - OY)
        if not rect_inside(loc, 0.5):
            return True
        return any(r[0] < o[2] and o[0] < r[2] and r[1] < o[3] and o[1] < r[3] for o in obst)

    missing = []
    order = sorted(fps, key=lambda f: (f.GetPosition().y, f.GetPosition().x))
    for f in order:
        ref = f.GetReference()
        num = int(''.join(c for c in ref if c.isdigit()) or 0)
        grouped = ((ref.startswith('R') and num >= 100) or (ref.startswith('D') and 7 <= num <= 14)
                   or ref in ('R4', 'R5', 'R6', 'R7', 'R8', 'R9'))
        fields = [f.Reference(), f.Value()]
        if ref in ('SW1', 'JP1', 'JP2'):
            f.Value().SetVisible(False)       # value is in the group label next to the part
            fields = [f.Reference()]
        if ref.startswith('H') or grouped or (ref.startswith('J') and not ref.startswith('JP')):
            for t in fields:      # holes; bank resistors / LEDs (group labels); connectors (port labels)
                t.SetVisible(False)
            continue
        cb = _crt(f)
        cx, cyy = (cb[0] + cb[2]) / 2, (cb[1] + cb[3]) / 2
        for t in fields:
            t.SetLayer(pcbnew.F_SilkS)
            t.SetVisible(True)
            t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
            t.SetTextThickness(mm(0.15))
            t.SetKeepUpright(True)
            done = False
            for gap in (0.15, 0.5, 1.0, 1.6, 2.2):
                for ang in (0, 90):
                    t.SetTextAngleDegrees(ang)
                    t.SetPosition(pcbnew.VECTOR2I(mm(cx), mm(cyy)))
                    tb = _box(t.GetBoundingBox())
                    tw, th = tb[2] - tb[0], tb[3] - tb[1]
                    cands = []
                    for sh in (0, -1.0, 1.0, -2.0, 2.0, -3.0, 3.0):
                        cands += [(cx + sh, cb[1] - gap - th / 2), (cx + sh, cb[3] + gap + th / 2),
                                  (cb[2] + gap + tw / 2, cyy + sh), (cb[0] - gap - tw / 2, cyy + sh)]
                    for (x, y) in cands:
                        r = (x - tw / 2, y - th / 2, x + tw / 2, y + th / 2)
                        if not hit(r):
                            t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
                            obst.append((r[0] - 0.1, r[1] - 0.1, r[2] + 0.1, r[3] + 0.1))
                            done = True
                            break
                    if done:
                        break
                if done:
                    break
            if not done:
                t.SetVisible(False)
                missing.append('%s.%s' % (ref, 'ref' if t is fields[0] else 'value'))
    return missing


# ===================================================================== netlist
def load_sch_nets():
    """(ref, pin) -> net name exactly as eeschema names it (incl. unconnected-(...) nets)."""
    from sexpr import parse, find, findall
    tree = parse(open(os.path.join(ROOT, 'out', 'check.net')).read())[0]
    out = {}
    for n in findall(find(tree, 'nets'), 'net'):
        for node in findall(n, 'node'):
            out[(str(find(node, 'ref')[1]), str(find(node, 'pin')[1]))] = str(find(n, 'name')[1])
    return out


SCH_NETS = load_sch_nets()


def add_outline(board):
    """Board outline (square when R_CORNER == 0, else straight edges with R arcs at the top corners)."""
    def seg(a, b):
        sg = pcbnew.PCB_SHAPE(board)
        sg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        sg.SetStart(V(*a))
        sg.SetEnd(V(*b))
        sg.SetLayer(pcbnew.Edge_Cuts)
        sg.SetWidth(pcbnew.FromMM(0.1))
        board.Add(sg)

    def arc(a, m, b):
        sg = pcbnew.PCB_SHAPE(board)
        sg.SetShape(pcbnew.SHAPE_T_ARC)
        sg.SetArcGeometry(V(*a), V(*m), V(*b))
        sg.SetLayer(pcbnew.Edge_Cuts)
        sg.SetWidth(pcbnew.FromMM(0.1))
        board.Add(sg)
    r = R_CORNER
    if r <= 0:
        for a, b in [((0, 0), (W, 0)), ((W, 0), (W, H)), ((W, H), (0, H)), ((0, H), (0, 0))]:
            seg(a, b)
        return
    k = r * (1 - math.cos(math.radians(45)))
    seg((r, 0), (W - r, 0))
    arc((W - r, 0), (W - k, k), (W, r))
    seg((W, r), (W, H))
    seg((W, H), (0, H))
    seg((0, H), (0, r))
    arc((0, r), (k, k), (r, 0))


def main():
    board = pcbnew.BOARD()
    pcbnew.SaveBoard(PCB, board)
    board = pcbnew.LoadBoard(PCB)
    board.GetDesignSettings().SetCopperLayerCount(2)

    nets = {}

    def net(name):
        if name not in nets:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            nets[name] = ni
        return nets[name]

    for p in design.PARTS:
        if not p['fp']:
            continue
        lib, name = p['fp'].split(':')
        path = os.path.join(ROOT, 'TestJig.pretty') if lib == 'TestJig' else os.path.join(FPDIR, lib + '.pretty')
        fp = pcbnew.FootprintLoad(path, name)
        if fp is None:
            raise SystemExit('footprint not found: ' + p['fp'])
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(p['ref'])
        fp.SetValue(p['value'])
        fp.SetPath(pcbnew.KIID_PATH('/' + U('sym' + p['ref'] + '1')))
        fp.SetSheetname('/')
        fp.SetSheetfile('TestJig.kicad_sch')
        board.Add(fp)
        x, y, rot, side = PLACE[p['ref']]
        fp.SetPosition(V(x, y))
        if side == 'B':
            fp.Flip(V(x, y), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        fp.SetOrientationDegrees(rot)
        for pad in fp.Pads():
            n = SCH_NETS.get((p['ref'], pad.GetNumber()))
            if n:
                pad.SetNet(net(n))

    missing = [p['ref'] for p in design.PARTS if p['fp'] and p['ref'] not in PLACE]
    if missing:
        raise SystemExit('no placement for %s' % missing)

    add_outline(board)
    for (t, x, y, size, ang) in SILK:
        add_text(board, t, x, y, size, ang)
    for (x0, y0, x1, y1) in ARROWS:
        add_arrow(board, x0, y0, x1, y1)
    add_marks(board)
    lost = place_fields(board)
    print('fields with no room (hidden):', ' '.join(lost) or 'none')

    board.SynchronizeNetsAndNetClasses(True)
    pcbnew.SaveBoard(PCB, board)
    add_zones()

    # placement checks: courtyard overlaps and parts outside the outline
    fps = list(board.GetFootprints())
    boxes = [(f.GetReference(),) + tuple(v - o for v, o in zip(_crt(f), (OX, OY, OX, OY))) for f in fps]
    over = 0
    for i in range(len(boxes)):
        a = boxes[i]
        if a[0] not in ('J13',) and not rect_inside(a[1:], 0.0):
            print('OUTSIDE', a[0], [round(v, 2) for v in a[1:]])
        for j in range(i + 1, len(boxes)):
            b = boxes[j]
            if a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4]:
                if 'U1' in (a[0], b[0]) and all(r not in ('H1', 'H3') for r in (a[0], b[0])):
                    continue  # low parts under the plugged-in DevKit are intended
                over += 1
                print('OVERLAP', a[0], b[0])
    print('footprints %d, nets %d, overlaps %d' % (len(fps), len(nets), over))


main()
import shutil  # noqa: E402
shutil.copy(os.path.join(ROOT, 'tools', 'project_template.kicad_pro'), os.path.join(ROOT, 'TestJig.kicad_pro'))
