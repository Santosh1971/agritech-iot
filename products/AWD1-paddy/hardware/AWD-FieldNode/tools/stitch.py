"""Drop GND stitching vias into the head wherever they clear every other net on every layer.

Run with KiCad's python after routing. Ties the F.Cu GND pour islands to the In1/B.Cu planes.
"""
import math, os, sys
import pcbnew
from pcbnew import FromMM as mm, ToMM
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
VIA_D, CLR, SPACING = 0.6, 0.25, 2.5

b = pcbnew.LoadBoard(F)
gnd = b.FindNet("GND")

obst, gvias = [], []        # (kind, geometry, half-width) in mm
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = (ToMM(t.GetPosition().x), ToMM(t.GetPosition().y))
        if t.GetNetname() == "GND":
            gvias.append(p)
        else:
            obst.append(("c", p, ToMM(t.GetWidth(pcbnew.F_Cu)) / 2))
    elif t.GetNetname() != "GND":
        obst.append(("s", (ToMM(t.GetStart().x), ToMM(t.GetStart().y), ToMM(t.GetEnd().x), ToMM(t.GetEnd().y)),
                     ToMM(t.GetWidth()) / 2))
for fp in b.GetFootprints():
    for p in fp.Pads():
        bb = p.GetBoundingBox()
        box = (ToMM(bb.GetX()), ToMM(bb.GetY()), ToMM(bb.GetRight()), ToMM(bb.GetBottom()))
        # GND pads are fine to sit next to, but never put a via inside any pad
        obst.append(("b", box, 0 if p.GetNetname() != "GND" else -CLR))
    if fp.GetReference() == "U1":       # nothing under the module body (keeps its bottom pads clean)
        obst.append(("b", (D.STRIP_CX - 7.7, D.HEAD[1], D.STRIP_CX + 7.7, D.HEAD[1] + 20.5), 0))

def seg_dist(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    L = dx * dx + dy * dy
    u = 0 if L == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L))
    return math.hypot(px - x1 - u * dx, py - y1 - u * dy)

def ok(x, y):
    need = VIA_D / 2 + CLR
    for kind, g, hw in obst:
        if kind == "c" and math.hypot(x - g[0], y - g[1]) < need + hw:
            return False
        if kind == "s" and seg_dist(x, y, *g) < need + hw:
            return False
        if kind == "b":
            m = need + hw
            if g[0] - m < x < g[2] + m and g[1] - m < y < g[3] + m:
                return False
    return all(math.hypot(x - a, y - c) >= SPACING for a, c in gvias)

hx0, hy0, hx1, hy1 = D.HEAD
added = 0
y = hy0 + 6.0                               # stay below the antenna keepout band
while y < hy1 - 1.0:
    x = hx0 + 1.0
    while x < hx1 - 1.0:
        if ok(x, y):
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y))); v.SetWidth(mm(VIA_D)); v.SetDrill(mm(0.3))
            v.SetNet(gnd); b.Add(v)
            gvias.append((x, y)); added += 1
        x += 0.5
    y += 0.5
print("stitching vias added:", added)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(F, b)
