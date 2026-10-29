"""Widen routed signal tracks to 0.5 mm wherever clearance allows (then 0.4, then 0.3 mm).

Freerouting routes at the 0.25 mm netclass width so it can thread between SOIC pins; this pass then
fattens every segment as far as the copper around it allows, which gives 0.5 mm on most runs and on
the approaches to through-hole pins. Run with KiCad's python after routing/fixup.
"""
import collections
import math
import os
import shutil

import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import OX, OY, inside  # noqa: E402
import wx  # noqa: E402
_APP = wx.App(False)
import pcbnew  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(ROOT, 'TestJig.kicad_pcb')
WIDTHS = (0.5, 0.4, 0.3)
PWR_WIDTHS = (1.0, 0.8, 0.6)   # +5V / +3V3
PWR_NETS = ('+5V', '+3V3')
CLR = 0.22                    # netclass clearance 0.2 mm + margin
EDGE = (50.0, 40.0, 150.0, 140.0)
EDGE_CLR = 0.35
CELL = 4.0
to = pcbnew.ToMM


def seg_seg(a, b, c, d):
    """Distance between segments ab and cd."""
    def pt_seg(p, s0, s1):
        dx, dy = s1[0] - s0[0], s1[1] - s0[1]
        L = dx * dx + dy * dy
        u = 0 if L == 0 else max(0, min(1, ((p[0] - s0[0]) * dx + (p[1] - s0[1]) * dy) / L))
        return math.hypot(p[0] - s0[0] - u * dx, p[1] - s0[1] - u * dy)

    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    d1, d2, d3, d4 = cross(c, d, a), cross(c, d, b), cross(a, b, c), cross(a, b, d)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(pt_seg(a, c, d), pt_seg(b, c, d), pt_seg(c, a, b), pt_seg(d, a, b))


def seg_rect(a, b, r):
    x0, y0, x1, y1 = r
    if (x0 <= a[0] <= x1 and y0 <= a[1] <= y1) or (x0 <= b[0] <= x1 and y0 <= b[1] <= y1):
        return 0.0
    edges = [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]
    return min(seg_seg(a, b, e0, e1) for e0, e1 in edges)


class Grid:
    def __init__(self):
        self.cells = collections.defaultdict(list)

    def keys(self, x0, y0, x1, y1):
        for i in range(int(x0 // CELL), int(x1 // CELL) + 1):
            for j in range(int(y0 // CELL), int(y1 // CELL) + 1):
                yield (i, j)

    def add(self, obj, bb):
        for k in self.keys(*bb):
            self.cells[k].append(obj)

    def near(self, bb):
        seen = set()
        for k in self.keys(*bb):
            for o in self.cells[k]:
                if id(o) not in seen:
                    seen.add(id(o))
                    yield o


def main():
    shutil.copy(os.path.join(ROOT, 'tools', 'project_template.kicad_pro'), os.path.join(ROOT, 'TestJig.kicad_pro'))
    b = pcbnew.LoadBoard(PCB)
    grid = {pcbnew.F_Cu: Grid(), pcbnew.B_Cu: Grid()}
    segs = []
    # obstacles: [kind, net, geometry, half_width]  (list so widths can be updated in place)
    for t in b.GetTracks():
        net = t.GetNetname()
        if t.GetClass() == 'PCB_VIA':
            p = (to(t.GetPosition().x), to(t.GetPosition().y))
            r = to(t.GetWidth(pcbnew.F_Cu)) / 2
            o = ['c', net, p, r]
            for ly in grid:
                grid[ly].add(o, (p[0] - r, p[1] - r, p[0] + r, p[1] + r))
        else:
            a = (to(t.GetStart().x), to(t.GetStart().y))
            c = (to(t.GetEnd().x), to(t.GetEnd().y))
            hw = to(t.GetWidth()) / 2
            o = ['s', net, (a, c), hw, t]
            grid[t.GetLayer()].add(o, (min(a[0], c[0]) - 1, min(a[1], c[1]) - 1, max(a[0], c[0]) + 1, max(a[1], c[1]) + 1))
            segs.append(o)
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNumber() == '' and not pad.HasHole():
                continue
            net = pad.GetNetname()
            bb = pad.GetBoundingBox()
            r = (to(bb.GetLeft()), to(bb.GetTop()), to(bb.GetRight()), to(bb.GetBottom()))
            if pad.GetShape() == pcbnew.PAD_SHAPE_CIRCLE:
                o = ['c', net, ((r[0] + r[2]) / 2, (r[1] + r[3]) / 2), (r[2] - r[0]) / 2]
            else:
                o = ['r', net, r, 0.0]
            for ly in grid:
                if pad.IsOnLayer(ly):
                    grid[ly].add(o, r)

    def ok(o, hw):
        (a, c) = o[2]
        ly = o[4].GetLayer()
        for f in (0.0, 0.25, 0.5, 0.75, 1.0):
            if not inside(a[0] + (c[0] - a[0]) * f - OX, a[1] + (c[1] - a[1]) * f - OY, hw + EDGE_CLR):
                return False
        reach = hw + CLR + 1.0
        bb = (min(a[0], c[0]) - reach, min(a[1], c[1]) - reach, max(a[0], c[0]) + reach, max(a[1], c[1]) + reach)
        for q in grid[ly].near(bb):
            if q is o or q[1] == o[1]:
                continue
            if q[0] == 's':
                d = seg_seg(a, c, q[2][0], q[2][1]) - q[3]
            elif q[0] == 'c':
                d = seg_seg(a, c, q[2], q[2]) - q[3]
            else:
                d = seg_rect(a, c, q[2])
            if d < hw + CLR:
                return False
        return True

    stats = collections.Counter()
    # longest first: long runs benefit most and get first pick of the space
    for o in sorted(segs, key=lambda s: -math.hypot(s[2][1][0] - s[2][0][0], s[2][1][1] - s[2][0][1])):
        cur = o[3] * 2
        widths = PWR_WIDTHS if o[1] in PWR_NETS else WIDTHS
        if cur >= widths[0] - 1e-6:
            stats['already max'] += 1
            continue
        for w in widths:
            if w <= cur + 1e-6:
                break
            if ok(o, w / 2):
                o[4].SetWidth(pcbnew.FromMM(w))
                o[3] = w / 2
                stats[('pwr %.1f' if o[1] in PWR_NETS else '%.1f') % w] += 1
                break
        else:
            stats['kept %.2f' % cur] += 1
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    shutil.copy(os.path.join(ROOT, 'tools', 'project_template.kicad_pro'), os.path.join(ROOT, 'TestJig.kicad_pro'))
    print('track widths:', dict(stats))


main()
