"""Tidy the silkscreen after routing (run with KiCad's python, after route.py and stitch.py).

  - each reference designator is tried around its part (above, below, left, right, then smaller);
    the first spot clear of pads, other silk and the board edge wins; if none is clear it is hidden
    (it is still on the assembly drawing, F.Fab)
"""
import os, sys
import pcbnew
from pcbnew import FromMM as mm, ToMM
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
F = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
b = pcbnew.LoadBoard(F)
hx0, hy0, hx1, hy1 = D.HEAD
EDGE, GAP = 0.3, 0.15

def box(bb, m=0.0):
    return (ToMM(bb.GetX()) - m, ToMM(bb.GetY()) - m, ToMM(bb.GetRight()) + m, ToMM(bb.GetBottom()) + m)

def hit(a, c):
    return a[0] < c[2] and a[2] > c[0] and a[1] < c[3] and a[3] > c[1]

# obstacles per side: exposed pads, footprint silk graphics, board texts
def side_of(layer):
    return "F" if layer in (pcbnew.F_SilkS, pcbnew.F_Cu) else "B"
obst = {"F": [], "B": []}
for fp in b.GetFootprints():
    for p in fp.Pads():
        for s, cu in (("F", pcbnew.F_Cu), ("B", pcbnew.B_Cu)):
            if p.IsOnLayer(cu) and (p.IsOnLayer(pcbnew.F_Mask if s == "F" else pcbnew.B_Mask)):
                obst[s].append(box(p.GetBoundingBox(), GAP))
    for g in fp.GraphicalItems():
        if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) and g.GetClass() == "PCB_SHAPE":
            obst[side_of(g.GetLayer())].append(box(g.GetBoundingBox(), GAP))
for d in b.GetDrawings():
    if d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
        obst[side_of(d.GetLayer())].append(box(d.GetBoundingBox(), GAP))
for t in b.GetTracks():                 # via annular rings are tented, but keep text off them anyway
    if t.GetClass() == "PCB_VIA":
        bb = box(t.GetBoundingBox(), 0.05)
        obst["F"].append(bb); obst["B"].append(bb)

def inside(bx):
    return hx0 + EDGE <= bx[0] and bx[2] <= hx1 - EDGE and hy0 + EDGE <= bx[1] and bx[3] <= hy1 - EDGE

moved = hidden = 0
fps = sorted(b.GetFootprints(), key=lambda f: -ToMM(f.GetBoundingBox(False).GetWidth()))
for fp in fps:
    ref = fp.Reference()
    if not ref.IsVisible() or fp.GetReference().startswith("E"):
        continue
    s = "B" if fp.IsFlipped() else "F"
    fp.BuildCourtyardCaches()
    c = fp.GetCourtyard(pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd)
    cb = box(c.BBox()) if c.OutlineCount() else box(fp.GetBoundingBox(False))
    cx, cy = (cb[0] + cb[2]) / 2, (cb[1] + cb[3]) / 2
    done = False
    for size in (0.8,):                     # JLC minimum readable silk height
        ref.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); ref.SetTextThickness(mm(size * 0.15))
        cands = []
        for d in (0.0, 0.3, 0.6):
            cands += [((cx, cb[1] - size / 2 - 0.1 - d), 0), ((cx, cb[3] + size / 2 + 0.1 + d), 0),
                      ((cb[0] - size / 2 - 0.1 - d, cy), 90), ((cb[2] + size / 2 + 0.1 + d, cy), 90)]
        for (x, y), rot in cands:
            ref.SetTextAngleDegrees(rot + (0 if True else 0))
            ref.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
            tb = box(ref.GetBoundingBox())
            if inside(tb) and not any(hit(tb, o) for o in obst[s]):
                obst[s].append(box(ref.GetBoundingBox(), GAP))
                done = True
                break
        if done:
            break
    if done:
        moved += 1
    else:
        ref.SetVisible(False)
        hidden += 1
print("references placed: %d, hidden: %d" % (moved, hidden))
pcbnew.SaveBoard(F, b)
