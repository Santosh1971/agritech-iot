"""Autoroute TestJig.kicad_pcb with Freerouting, then fill the GND pours.

Run with KiCad's python:  <KiCad python3> tools/route.py [max_passes]
"""
import os
import shutil
import subprocess
import sys

import wx  # noqa: E402
_APP = wx.App(False)
import pcbnew  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(ROOT, 'TestJig.kicad_pcb')
RDIR = os.path.join(ROOT, 'route')
JAR = os.path.expanduser('~/Applications/freerouting/freerouting-1.9.0.jar')
PASSES = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != 'post' else '60'


sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import OX, OY, W, H, ANTENNA_KO as KEEPOUT, inside  # noqa: E402
VIA_D, CLR, PITCH = 0.6, 0.3, 2.2


def stitch(b):
    """GND vias on a grid wherever they clear every other net, so both pours stay tied together."""
    mm, to = pcbnew.FromMM, pcbnew.ToMM
    gnd = b.FindNet('GND')
    segs, circ, boxes = [], [], []
    for t in b.GetTracks():
        if t.GetClass() == 'PCB_VIA':
            circ.append((to(t.GetPosition().x), to(t.GetPosition().y), to(t.GetWidth(pcbnew.F_Cu)) / 2))
        elif t.GetNetname() != 'GND':
            segs.append((to(t.GetStart().x), to(t.GetStart().y), to(t.GetEnd().x), to(t.GetEnd().y), to(t.GetWidth()) / 2))
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            bb = pad.GetBoundingBox()
            boxes.append((to(bb.GetX()), to(bb.GetY()), to(bb.GetRight()), to(bb.GetBottom())))
        if fp.GetReference().startswith('H'):
            c = fp.GetPosition()
            circ.append((to(c.x), to(c.y), 3.6))

    def seg_d(px, py, x1, y1, x2, y2):
        dx, dy = x2 - x1, y2 - y1
        L = dx * dx + dy * dy
        u = 0 if L == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L))
        return ((px - x1 - u * dx) ** 2 + (py - y1 - u * dy) ** 2) ** 0.5

    need = VIA_D / 2 + CLR
    added = 0
    y = 1.5
    while y < H - 1.5:
        x = 1.5
        while x < W - 1.5:
            X, Y = OX + x, OY + y
            k = KEEPOUT
            ok = inside(x, y, 1.2) and not (k[0] - 1 < x < k[2] + 1 and k[1] - 1 < y < k[3] + 1)
            ok = ok and all(((X - cx) ** 2 + (Y - cy) ** 2) ** 0.5 >= need + r for cx, cy, r in circ)
            ok = ok and all(seg_d(X, Y, *sg[:4]) >= need + sg[4] for sg in segs)
            ok = ok and all(not (b0 - need < X < b2 + need and b1 - need < Y < b3 + need) for b0, b1, b2, b3 in boxes)
            if ok:
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pcbnew.VECTOR2I(mm(X), mm(Y)))
                v.SetWidth(mm(VIA_D))
                v.SetDrill(mm(0.3))
                v.SetNet(gnd)
                b.Add(v)
                circ.append((X, Y, VIA_D / 2 + PITCH - need))
                added += 1
            x += 0.5
        y += 0.5
    return added


def restore_pro():
    shutil.copy(os.path.join(ROOT, 'tools', 'project_template.kicad_pro'), os.path.join(ROOT, 'TestJig.kicad_pro'))


def freeroute(b, tag, passes):
    dsn, ses = os.path.join(RDIR, tag + '.dsn'), os.path.join(RDIR, tag + '.ses')
    if os.path.exists(ses):
        os.remove(ses)
    if not pcbnew.ExportSpecctraDSN(b, dsn):
        raise SystemExit('DSN export failed')
    with open(os.path.join(RDIR, tag + '.log'), 'w') as log:
        subprocess.run(['java', '-jar', JAR, '-de', dsn, '-do', ses, '-mp', str(passes)],
                       cwd=RDIR, stdout=log, stderr=subprocess.STDOUT, timeout=1500)
    if not os.path.exists(ses) or not pcbnew.ImportSpecctraSES(b, ses):
        raise SystemExit('freerouting/SES import failed for ' + tag)


class Obst:
    """Copper of every net except GND, for clearance checks in mm."""

    def __init__(self, b):
        to = pcbnew.ToMM
        self.segs, self.circ, self.boxes = [], [], []
        for t in b.GetTracks():
            if t.GetNetname() == 'GND':
                continue
            if t.GetClass() == 'PCB_VIA':
                self.circ.append((to(t.GetPosition().x), to(t.GetPosition().y), to(t.GetWidth(pcbnew.F_Cu)) / 2))
            else:
                self.segs.append((to(t.GetStart().x), to(t.GetStart().y), to(t.GetEnd().x), to(t.GetEnd().y),
                                  to(t.GetWidth()) / 2, t.GetLayer()))
        for fp in b.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() == 'GND' or pad.GetNumber() == '':
                    continue   # own net, or paste-only aperture pads (SOD-882) / NPTH holes
                bb = pad.GetBoundingBox()
                self.boxes.append((to(bb.GetX()), to(bb.GetY()), to(bb.GetRight()), to(bb.GetBottom()),
                                   pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD, pad.IsOnLayer(pcbnew.B_Cu)))
            if fp.GetReference().startswith('H'):
                c = fp.GetPosition()
                self.circ.append((to(c.x), to(c.y), 3.3))

    @staticmethod
    def seg_d(px, py, x1, y1, x2, y2):
        dx, dy = x2 - x1, y2 - y1
        L = dx * dx + dy * dy
        u = 0 if L == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L))
        return ((px - x1 - u * dx) ** 2 + (py - y1 - u * dy) ** 2) ** 0.5

    def point_ok(self, x, y, r, layer=None):
        """Clear of all copper within radius r (layer None = both layers, i.e. a via)."""
        for cx, cy, cr in self.circ:
            if ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 < r + cr:
                return False
        for x1, y1, x2, y2, hw, ly in self.segs:
            if (layer is None or ly == layer) and self.seg_d(x, y, x1, y1, x2, y2) < r + hw:
                return False
        for b0, b1, b2, b3, smd, back in self.boxes:
            if layer is not None and smd and not ((layer == pcbnew.B_Cu) == back):
                continue
            if layer is None and smd and back:
                pass
            if b0 - r < x < b2 + r and b1 - r < y < b3 + r:
                return False
        return True

    def track_ok(self, x1, y1, x2, y2, hw, layer):
        n = max(2, int(((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5 / 0.15))
        return all(self.point_ok(x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n, hw, layer) for i in range(n + 1))


def fanout_gnd(b):
    """Short track + via from every GND SMD pad, so no GND pad depends on a single pour island."""
    mm, to = pcbnew.FromMM, pcbnew.ToMM
    ob = Obst(b)
    gnd = b.FindNet('GND')
    added = 0
    import math
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() != 'GND' or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            px, py = to(pad.GetPosition().x), to(pad.GetPosition().y)
            layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
            done = False
            for dist in (1.2, 1.6, 2.0, 2.5, 3.0):
                for k in range(16):
                    a = 2 * math.pi * k / 16
                    vx, vy = px + dist * math.cos(a), py + dist * math.sin(a)
                    if not inside(vx - OX, vy - OY, 1.2):
                        continue
                    if KEEPOUT[0] + OX - 1 < vx < KEEPOUT[2] + OX + 1 and KEEPOUT[1] + OY - 1 < vy < KEEPOUT[3] + OY + 1:
                        continue
                    if not ob.point_ok(vx, vy, VIA_D / 2 + CLR):
                        continue
                    if not ob.track_ok(px, py, vx, vy, 0.2 + CLR, layer):
                        continue
                    t = pcbnew.PCB_TRACK(b)
                    t.SetStart(pad.GetPosition())
                    t.SetEnd(pcbnew.VECTOR2I(mm(vx), mm(vy)))
                    t.SetWidth(mm(0.4))
                    t.SetLayer(layer)
                    t.SetNet(gnd)
                    t.SetLocked(True)
                    b.Add(t)
                    v = pcbnew.PCB_VIA(b)
                    v.SetPosition(pcbnew.VECTOR2I(mm(vx), mm(vy)))
                    v.SetWidth(mm(VIA_D))
                    v.SetDrill(mm(0.3))
                    v.SetNet(gnd)
                    v.SetLocked(True)
                    b.Add(v)
                    ob.circ.append((vx, vy, VIA_D / 2))   # keep later fanout vias apart
                    added += 1
                    done = True
                    break
                if done:
                    break
            if not done:
                print('  no fanout for %s pad %s' % (fp.GetReference(), pad.GetNumber()))
    return added


def prune_dangling(b):
    """Drop stitching vias (no tracks) that end up outside the filled pour on either layer."""
    to = pcbnew.ToMM
    zones = {pcbnew.F_Cu: [], pcbnew.B_Cu: []}
    for z in b.Zones():
        if z.GetNetname() == 'GND':
            zones[z.GetLayer()].append(z)
    ends = set()
    for t in b.GetTracks():
        if t.GetClass() == 'PCB_TRACK':
            ends.add((t.GetStart().x, t.GetStart().y))
            ends.add((t.GetEnd().x, t.GetEnd().y))
    removed = 0
    for t in list(b.GetTracks()):
        if t.GetClass() != 'PCB_VIA' or t.GetNetname() != 'GND':
            continue
        pos = t.GetPosition()
        if (pos.x, pos.y) in ends:
            continue
        ok = all(any(z.GetFilledPolysList(ly).Contains(pos) for z in zs) for ly, zs in zones.items())
        if not ok:
            b.Remove(t)
            removed += 1
    return removed


if len(sys.argv) > 1 and sys.argv[1] == 'post':
    # separate process: after Remove()/SES import the swig board object is unreliable for a reload
    restore_pro()
    b = pcbnew.LoadBoard(PCB)
    print('stitching vias:', stitch(b))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    print('dangling vias removed:', prune_dangling(b))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    restore_pro()
    print('post done: %d track/via items' % len(b.GetTracks()))
    sys.exit(0)

os.makedirs(RDIR, exist_ok=True)
restore_pro()
b = pcbnew.LoadBoard(PCB)
for t in list(b.GetTracks()):
    b.Remove(t)
print('gnd fanouts (pre-route, locked):', fanout_gnd(b))
freeroute(b, 'pass1', PASSES)
freeroute(b, 'pass2', 30)
pcbnew.SaveBoard(PCB, b)
restore_pro()
print('routing saved')
r = subprocess.run([sys.executable, '-u', os.path.abspath(__file__), 'post'], stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT, universal_newlines=True)
print('\n'.join(l for l in r.stdout.splitlines() if not l.startswith(('swig', '1', '0', '2')) and 'Debug' not in l))
