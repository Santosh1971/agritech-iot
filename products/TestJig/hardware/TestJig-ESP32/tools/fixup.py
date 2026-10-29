"""Finish what the autorouter left: route leftover pad pairs from the DRC report with a small
grid maze router, and tie orphan GND pour fragments to the other layer with a via.

Run with KiCad's python after `kicad-cli pcb drc ... -o out/drc.rpt`:
    <KiCad python3> tools/fixup.py
"""
import collections
import heapq
import math
import os
import re
import shutil

import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board import OX, OY, inside  # noqa: E402
import wx  # noqa: E402
_APP = wx.App(False)
import pcbnew  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(ROOT, 'TestJig.kicad_pcb')
RPT = os.path.join(ROOT, 'out', 'drc.rpt')
G = 0.1                     # grid mm
TRACK_W, CLR, VIA_D, VIA_DR = 0.25, 0.22, 0.6, 0.3
mm, to = pcbnew.FromMM, pcbnew.ToMM
LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu]


def restore_pro():
    shutil.copy(os.path.join(ROOT, 'tools', 'project_template.kicad_pro'), os.path.join(ROOT, 'TestJig.kicad_pro'))


def seg_d(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    L = dx * dx + dy * dy
    u = 0 if L == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L))
    return math.hypot(px - x1 - u * dx, py - y1 - u * dy)


def stitch_vias(b):
    """GND vias with no track ending on them (pure pour stitching, safe to move/drop)."""
    ends = set()
    for t in b.GetTracks():
        if t.GetClass() == 'PCB_TRACK':
            ends.add((t.GetStart().x, t.GetStart().y))
            ends.add((t.GetEnd().x, t.GetEnd().y))
    return [t for t in b.GetTracks() if t.GetClass() == 'PCB_VIA' and t.GetNetname() == 'GND'
            and (t.GetPosition().x, t.GetPosition().y) not in ends]


def copper_items(b, netname, skip=()):
    """Other-net copper as (layers, kind, geometry)."""
    items = []
    skip_ids = {id(x) for x in skip}
    skip_pos = {(x.GetPosition().x, x.GetPosition().y) for x in skip}
    for t in b.GetTracks():
        if t.GetNetname() == netname:
            continue
        if t.GetClass() == 'PCB_VIA' and (t.GetPosition().x, t.GetPosition().y) in skip_pos:
            continue
        if t.GetClass() == 'PCB_VIA':
            items.append((LAYERS, 'c', (to(t.GetPosition().x), to(t.GetPosition().y), to(t.GetWidth(pcbnew.F_Cu)) / 2)))
        else:
            items.append(([t.GetLayer()], 's', (to(t.GetStart().x), to(t.GetStart().y), to(t.GetEnd().x),
                                                to(t.GetEnd().y), to(t.GetWidth()) / 2)))
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname() == netname and p.GetNumber() != '':
                continue
            bb = p.GetBoundingBox()
            lys = [ly for ly in LAYERS if p.IsOnLayer(ly)] or LAYERS
            if p.GetNumber() == '' and not p.HasHole():
                continue  # paste-only aperture
            items.append((lys, 'b', (to(bb.GetX()), to(bb.GetY()), to(bb.GetRight()), to(bb.GetBottom()))))
    return items


def blocked_grid(items, x0, y0, nx, ny, margin):
    """Set of (layer_index, i, j) cells closer than `margin` to any item."""
    blk = set()
    for lys, kind, g in items:
        if kind == 'b':
            bx0, by0, bx1, by1 = g[0] - margin, g[1] - margin, g[2] + margin, g[3] + margin
        elif kind == 'c':
            bx0, by0, bx1, by1 = g[0] - g[2] - margin, g[1] - g[2] - margin, g[0] + g[2] + margin, g[1] + g[2] + margin
        else:
            bx0, by0 = min(g[0], g[2]) - g[4] - margin, min(g[1], g[3]) - g[4] - margin
            bx1, by1 = max(g[0], g[2]) + g[4] + margin, max(g[1], g[3]) + g[4] + margin
        i0, i1 = max(0, int((bx0 - x0) / G)), min(nx - 1, int((bx1 - x0) / G) + 1)
        j0, j1 = max(0, int((by0 - y0) / G)), min(ny - 1, int((by1 - y0) / G) + 1)
        for i in range(i0, i1 + 1):
            x = x0 + i * G
            for j in range(j0, j1 + 1):
                y = y0 + j * G
                if kind == 'b':
                    hit = True
                elif kind == 'c':
                    hit = math.hypot(x - g[0], y - g[1]) < g[2] + margin
                else:
                    hit = seg_d(x, y, *g[:4]) < g[4] + margin
                if hit:
                    for ly in lys:
                        blk.add((LAYERS.index(ly), i, j))
    return blk


def maze(b, net, a, a_layers, z, z_layers, pad=8.0, skip=()):
    """A* on a 2-layer grid from point a to point z (mm). Returns list of (layer_idx, x, y) or None."""
    x0, y0 = min(a[0], z[0]) - pad, min(a[1], z[1]) - pad
    nx, ny = int((abs(a[0] - z[0]) + 2 * pad) / G) + 1, int((abs(a[1] - z[1]) + 2 * pad) / G) + 1
    items = copper_items(b, net.GetNetname(), skip)
    tblk = blocked_grid(items, x0, y0, nx, ny, TRACK_W / 2 + CLR)
    vblk = blocked_grid(items, x0, y0, nx, ny, VIA_D / 2 + CLR)
    # no new via right next to an existing drilled item of this net (drill-to-drill spacing)
    drills = [(to(t.GetPosition().x), to(t.GetPosition().y)) for t in b.GetTracks()
              if t.GetClass() == 'PCB_VIA' and t.GetNetname() == net.GetNetname()]
    drills += [(to(p.GetPosition().x), to(p.GetPosition().y)) for fp in b.GetFootprints() for p in fp.Pads()
               if p.HasHole() and p.GetNetname() == net.GetNetname()]
    for (vx, vy) in drills:
        if True:
            for i in range(max(0, int((vx - 1.0 - x0) / G)), min(nx, int((vx + 1.0 - x0) / G) + 1)):
                for j in range(max(0, int((vy - 1.0 - y0) / G)), min(ny, int((vy + 1.0 - y0) / G) + 1)):
                    if 0.05 < math.hypot(x0 + i * G - vx, y0 + j * G - vy) < 1.4:
                        vblk.add((0, i, j))
                        vblk.add((1, i, j))
    for i in range(nx):                      # stay inside the (rounded) board outline
        for j in range(ny):
            if not inside(x0 + i * G - OX, y0 + j * G - OY, 0.6):
                for L in (0, 1):
                    tblk.add((L, i, j))
                    vblk.add((L, i, j))
    cell = lambda p: (int(round((p[0] - x0) / G)), int(round((p[1] - y0) / G)))  # noqa: E731
    ai, aj = cell(a)
    zi, zj = cell(z)
    goal = {(LAYERS.index(ly), zi, zj) for ly in z_layers}
    start = [(LAYERS.index(ly), ai, aj) for ly in a_layers]
    h = lambda n: math.hypot(n[1] - zi, n[2] - zj)  # noqa: E731
    openq, came, cost = [], {}, {}
    for s in start:
        cost[s] = 0
        heapq.heappush(openq, (h(s), s))
    steps = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1), (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
    while openq:
        _, n = heapq.heappop(openq)
        if n in goal:
            path = [n]
            while n in came:
                n = came[n]
                path.append(n)
            pts = [(ly, x0 + i * G, y0 + j * G) for ly, i, j in reversed(path)]
            # join the exact pad centres with short same-layer stubs (vias only ever sit on grid points)
            return [(pts[0][0], a[0], a[1])] + pts + [(pts[-1][0], z[0], z[1])]
        ly, i, j = n
        nbrs = []
        for di, dj, c in steps:
            m = (ly, i + di, j + dj)
            if 0 <= m[1] < nx and 0 <= m[2] < ny and m not in tblk:
                nbrs.append((m, c))
        m = (1 - ly, i, j)
        if (ly, i, j) not in vblk and (m[0], i, j) not in vblk:
            nbrs.append((m, 25))
        for m, c in nbrs:
            nc = cost[n] + c
            if nc < cost.get(m, 1e18):
                cost[m] = nc
                came[m] = n
                heapq.heappush(openq, (nc + h(m), m))
    return None


def _has_hole_at(b, x, y, r=0.75):
    """A drilled item (through-hole pad or via) already sits within r mm of (x, y)."""
    for t in b.GetTracks():
        if t.GetClass() == 'PCB_VIA' and math.hypot(to(t.GetPosition().x) - x, to(t.GetPosition().y) - y) < r:
            return True
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.HasHole() and math.hypot(to(p.GetPosition().x) - x, to(p.GetPosition().y) - y) < r:
                return True
    return False


def dedupe_vias(b):
    """Drop vias that sit on a through-hole pad or on another via (drill-to-drill too close)."""
    holes = [(to(p.GetPosition().x), to(p.GetPosition().y)) for fp in b.GetFootprints() for p in fp.Pads() if p.HasHole()]
    kept, removed = [], 0
    for t in list(b.GetTracks()):
        if t.GetClass() != 'PCB_VIA':
            continue
        x, y = to(t.GetPosition().x), to(t.GetPosition().y)
        if any(math.hypot(x - hx, y - hy) < 0.05 for hx, hy in holes) or any(math.hypot(x - kx, y - ky) < 0.05 for kx, ky in kept):
            b.Remove(t)
            removed += 1
        else:
            kept.append((x, y))
    return removed


def add_path(b, net, path):
    """Collapse the grid path into straight segments + vias."""
    def emit_seg(p, q):
        if (p[1], p[2]) == (q[1], q[2]):
            return
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(pcbnew.VECTOR2I(mm(p[1]), mm(p[2])))
        t.SetEnd(pcbnew.VECTOR2I(mm(q[1]), mm(q[2])))
        t.SetWidth(mm(TRACK_W))
        t.SetLayer(LAYERS[p[0]])
        t.SetNet(net)
        b.Add(t)

    run_start = path[0]
    prev = path[0]
    prev_dir = None
    n = 0
    for cur in path[1:]:
        if cur[0] != prev[0]:          # layer change -> via (unless a through-hole pad or via is already there)
            emit_seg(run_start, prev)
            if not _has_hole_at(b, prev[1], prev[2], r=0.05):
                v = pcbnew.PCB_VIA(b)
                v.SetPosition(pcbnew.VECTOR2I(mm(prev[1]), mm(prev[2])))
                v.SetWidth(mm(VIA_D))
                v.SetDrill(mm(VIA_DR))
                v.SetNet(net)
                b.Add(v)
            run_start, prev, prev_dir = cur, cur, None
            continue
        d = (round((cur[1] - prev[1]) / G), round((cur[2] - prev[2]) / G))
        if cur is path[-1] or prev is path[0]:
            d = None    # pad-centre stubs are off-grid; always break the run there
        if prev_dir is not None and d != prev_dir:
            emit_seg(run_start, prev)
            run_start = prev
            n += 1
        prev_dir = d
        prev = cur
    emit_seg(run_start, prev)


def pad_by(b, ref, num):
    fp = b.FindFootprintByReference(ref)
    for p in fp.Pads():
        if p.GetNumber() == num:
            return p
    raise KeyError((ref, num))


def endpoint(b, desc, at):
    m = re.match(r'(?:PTH )?[Pp]ad (\S+) \[([^\]]*)\] of (\S+)', desc)
    if m:
        p = pad_by(b, m.group(3), m.group(1))
        lys = [ly for ly in LAYERS if p.IsOnLayer(ly)]
        return (to(p.GetPosition().x), to(p.GetPosition().y)), lys, m.group(2)
    m = re.match(r'Track \[([^\]]*)\] on (\S+)', desc)
    if m:
        ly = pcbnew.F_Cu if m.group(2) == 'F.Cu' else pcbnew.B_Cu
        return at, [ly], m.group(1)
    return None


def gnd_components(b, netname='GND'):
    """Union-find over the copper of one net (default GND): pour fragments, vias, through-hole pads, tracks and SMD pads.
    Returns (frags, find) where frags = [(layer, outline)] indexed like the union-find nodes 0..n-1."""
    frags = []
    for z in b.Zones():
        if z.GetNetname() != netname:
            continue
        ly = z.GetLayer()
        polys = z.GetFilledPolysList(ly)
        for k in range(polys.OutlineCount()):
            frags.append((ly, polys.Outline(k)))
    parent = list(range(len(frags)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def new_node():
        parent.append(len(parent))
        return len(parent) - 1

    def union(i, j):
        parent[find(i)] = find(j)

    def frag_at(ly, pt):
        return [i for i, (fl, o) in enumerate(frags) if fl == ly and o.PointInside(pt)]

    points = []   # (layer or None for both, VECTOR2I, node)
    objs = []     # (kind, object, node)
    for t in b.GetTracks():
        if t.GetNetname() != netname:
            continue
        n = new_node()
        objs.append(('via' if t.GetClass() == 'PCB_VIA' else 'track', t, n))
        if t.GetClass() == 'PCB_VIA':
            points.append((None, t.GetPosition(), n))
        else:
            points.append((t.GetLayer(), t.GetStart(), n))
            points.append((t.GetLayer(), t.GetEnd(), n))
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() != netname:
                continue
            n = new_node()
            objs.append(('pad', pad, n))
            lys = None if pad.HasHole() else (pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu)
            points.append((lys, pad.GetPosition(), n))
    for ly, pt, n in points:
        for L in ([ly] if ly is not None else LAYERS):
            for i in frag_at(L, pt):
                union(n, i)
    # coincident items (track end on a via or pad)
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            li, pi, ni = points[i]
            lj, pj, nj = points[j]
            if (li is None or lj is None or li == lj) and abs(pi.x - pj.x) < 50000 and abs(pi.y - pj.y) < 50000:
                union(ni, nj)
    return frags, find, objs


def fix_islands(b):
    """Put a via in every GND pour fragment that is not tied to the main ground, at a spot where the
    other layer's pour belongs to the main ground."""
    gnd = b.FindNet('GND')
    added = 0
    for _ in range(10):
        frags, find, _ = gnd_components(b)
        if not frags:
            return added
        size = collections.Counter(find(i) for i in range(len(frags)))
        main = size.most_common(1)[0][0]
        orphans = [i for i in range(len(frags)) if find(i) != main]
        if not orphans:
            return added
        items = copper_items(b, 'GND')
        progress = False
        for i in orphans:
            ly, outline = frags[i]
            other = LAYERS[1 - LAYERS.index(ly)]
            targets = [o for j, (fl, o) in enumerate(frags) if fl == other and find(j) == main]
            bb = outline.BBox()
            y = bb.GetTop() + mm(0.4)
            placed = False
            while y < bb.GetBottom() and not placed:
                x = bb.GetLeft() + mm(0.4)
                while x < bb.GetRight() and not placed:
                    pt = pcbnew.VECTOR2I(int(x), int(y))
                    ring = [pcbnew.VECTOR2I(int(x + dx), int(y + dy)) for dx, dy in
                            [(mm(0.35), 0), (-mm(0.35), 0), (0, mm(0.35)), (0, -mm(0.35))]]
                    if all(outline.PointInside(q) for q in [pt] + ring) and \
                            any(all(o.PointInside(q) for q in [pt] + ring) for o in targets) and \
                            not _has_hole_at(b, to(x), to(y), r=1.4):
                        X, Y = to(x), to(y)
                        r = VIA_D / 2 + CLR
                        ok = True
                        for lys, kind, g in items:
                            if kind == 'b' and g[0] - r < X < g[2] + r and g[1] - r < Y < g[3] + r:
                                ok = False
                            elif kind == 'c' and math.hypot(X - g[0], Y - g[1]) < r + g[2]:
                                ok = False
                            elif kind == 's' and seg_d(X, Y, *g[:4]) < r + g[4]:
                                ok = False
                            if not ok:
                                break
                        if ok:
                            v = pcbnew.PCB_VIA(b)
                            v.SetPosition(pt)
                            v.SetWidth(mm(VIA_D))
                            v.SetDrill(mm(VIA_DR))
                            v.SetNet(gnd)
                            b.Add(v)
                            added += 1
                            placed = progress = True
                    x += mm(0.2)
                y += mm(0.2)
            if placed:
                break     # connectivity changed; recompute
            print('  orphan GND fragment on %s near (%.1f, %.1f) mm: no via spot' %
                  ('F.Cu' if ly == pcbnew.F_Cu else 'B.Cu', to(bb.GetCenter().x), to(bb.GetCenter().y)))
        if not progress:
            return added
        pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    return added


def prune_orphan_vias(b):
    """Remove GND stitch vias that only hold up pour islands with no pad on them."""
    removed = 0
    for _ in range(5):
        frags, find, objs = gnd_components(b)
        comp_size = collections.Counter(find(i) for i in range(len(frags)))
        main = comp_size.most_common(1)[0][0]
        has_pad = {find(n) for kind, o, n in objs if kind == 'pad'}
        dead = [o for kind, o, n in objs if kind == 'via' and find(n) != main and find(n) not in has_pad]
        if not dead:
            break
        for v in dead:
            b.Remove(v)
            removed += 1
        pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    return removed


def bridge_islands(b):
    """Maze-route a GND track from each isolated GND island (one that holds vias/tracks) to the
    nearest via of the main ground."""
    bridged = 0
    for _ in range(6):
        frags, find, objs = gnd_components(b)
        size = collections.Counter(find(i) for i in range(len(frags)))
        main = size.most_common(1)[0][0]
        islands = collections.defaultdict(list)
        for kind, o, n in objs:
            if kind == 'via' and find(n) != main:
                islands[find(n)].append(o)
        main_vias = [o for kind, o, n in objs if kind == 'via' and find(n) == main]
        if not islands:
            break
        gnd = b.FindNet('GND')
        done_one = False
        for comp, vias in islands.items():
            a = (to(vias[0].GetPosition().x), to(vias[0].GetPosition().y))
            cands = sorted(main_vias, key=lambda v: math.hypot(to(v.GetPosition().x) - a[0], to(v.GetPosition().y) - a[1]))
            for v in cands[:8]:
                z = (to(v.GetPosition().x), to(v.GetPosition().y))
                path = maze(b, gnd, a, LAYERS, z, LAYERS)
                if path:
                    add_path(b, gnd, path)
                    bridged += 1
                    done_one = True
                    print('  bridged GND island at (%.1f, %.1f) to via at (%.1f, %.1f)' % (a[0] - 50, a[1] - 40, z[0] - 50, z[1] - 40))
                    break
            if done_one:
                break
        if not done_one:
            # closest-pair join between this island and the main ground (pads, vias, track ends)
            for comp, vias in islands.items():
                p0 = (to(vias[0].GetPosition().x), to(vias[0].GetPosition().y), False)
                if join(b, 'GND', p0, (0, 0, True)):
                    bridged += 1
                    done_one = True
                    print('  joined GND island at (%.1f, %.1f) to main ground' % (p0[0] - 50, p0[1] - 40))
                    break
        if not done_one:
            break
        pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    return bridged


def join(b, netname, pa, pb):
    """Connect the two copper islands of `netname` that contain points pa / pb (x, y, is_zone).
    Tries the closest pad/via/track-end pairs between the islands, then the GND stitch-field fallback."""
    frags, find, objs = gnd_components(b, netname)
    comps = collections.Counter(find(n) for _, _, n in objs)
    for i in range(len(frags)):
        comps[find(i)] += 1
    main = comps.most_common(1)[0][0]

    def items():
        for kind, o, n in objs:
            if kind == 'pad':
                lys = [ly for ly in LAYERS if o.IsOnLayer(ly)]
                yield find(n), (to(o.GetPosition().x), to(o.GetPosition().y)), lys
            elif kind == 'via':
                yield find(n), (to(o.GetPosition().x), to(o.GetPosition().y)), LAYERS
            else:
                for q in (o.GetStart(), o.GetEnd()):
                    yield find(n), (to(q.x), to(q.y)), [o.GetLayer()]
    allitems = list(items())

    def comp_of(p):
        if p[2]:
            return main
        best = min(allitems, key=lambda it: math.hypot(it[1][0] - p[0], it[1][1] - p[1]))
        return best[0]
    ca, cb = comp_of(pa), comp_of(pb)
    if ca == cb:
        return False
    A = [it for it in allitems if it[0] == ca]
    B = [it for it in allitems if it[0] == cb]
    pairs = sorted(((math.hypot(x[1][0] - y[1][0], x[1][1] - y[1][1]), x, y) for x in A for y in B), key=lambda t: t[0])
    net = b.FindNet(netname)
    tried = set()
    for d, x, y in pairs[:60]:
        key = (round(x[1][0], 1), round(x[1][1], 1), round(y[1][0], 1), round(y[1][1], 1))
        if key in tried:
            continue
        tried.add(key)
        if len(tried) > 8:
            break
        path = maze(b, net, x[1], x[2], y[1], y[2], pad=6.0)
        if path:
            add_path(b, net, path)
            return True
    # last resort through the stitch-via field
    for d, x, y in pairs[:3]:
        sv = stitch_vias(b)
        path = maze(b, net, x[1], x[2], y[1], y[2], pad=10.0, skip=sv)
        if path:
            need = VIA_D / 2 + CLR + TRACK_W / 2
            pts = [(q[1], q[2]) for q in path]
            for v in sv:
                vx, vy = to(v.GetPosition().x), to(v.GetPosition().y)
                if any(seg_d(vx, vy, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]) < need
                       for i in range(len(pts) - 1)):
                    b.Remove(v)
            add_path(b, net, path)
            return True
    return False


def _island_boxes(b, margin):
    frags, find, objs = gnd_components(b)
    size = collections.Counter(find(i) for i in range(len(frags)))
    main = size.most_common(1)[0][0]
    boxes = []
    for i, (ly, o) in enumerate(frags):
        if find(i) != main and any(find(n) == find(i) for k, _, n in objs if k in ('via', 'pad')):
            bb = o.BBox()
            boxes.append((to(bb.GetLeft()) - margin, to(bb.GetTop()) - margin,
                          to(bb.GetRight()) + margin, to(bb.GetBottom()) + margin))
    return boxes


def relax_near_islands(b):
    """If a GND island is still cut off, narrow the signal tracks right around it a step at a time
    (0.4, then 0.25 mm; small margin first) until the pour flows back in. Keeps most of the widening."""
    changed = 0
    for margin in (0.6, 1.2, 2.5):
        for base in (0.4, 0.25):
            boxes = _island_boxes(b, margin)
            if not boxes:
                return changed
            n0 = changed
            for t in b.GetTracks():
                if t.GetClass() != 'PCB_TRACK' or t.GetNetname() in ('GND', '+5V', '+3V3') or to(t.GetWidth()) <= base + 1e-6:
                    continue
                xs = (to(t.GetStart().x), to(t.GetEnd().x))
                ys = (to(t.GetStart().y), to(t.GetEnd().y))
                if any(min(xs) < x1 and max(xs) > x0 and min(ys) < y1 and max(ys) > y0 for x0, y0, x1, y1 in boxes):
                    t.SetWidth(pcbnew.FromMM(base))
                    changed += 1
            if changed != n0:
                pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    return changed


def main():
    restore_pro()
    b = pcbnew.LoadBoard(PCB)
    txt = open(RPT).read()
    routed = 0
    for blk in re.split(r'\n(?=\[)', txt):
        if not blk.startswith('[unconnected_items'):
            continue
        ends = re.findall(r'@\(([-\d.]+) mm, ([-\d.]+) mm\): (.*)', blk)
        if len(ends) != 2:
            continue
        nets = [re.search(r'\[([^\]]*)\]', e[2]) for e in ends]
        if not all(nets) or nets[0].group(1) != nets[1].group(1):
            print('skip', ends)
            continue
        if all(e[2].startswith('Zone') for e in ends):
            continue                       # pour-to-pour gaps are handled by the island passes below
        netname = nets[0].group(1)
        pts = [(float(e[0]), float(e[1]), e[2].startswith('Zone')) for e in ends]
        if join(b, netname, pts[0], pts[1]):
            routed += 1
            print('routed', netname)
        else:
            print('NO PATH for', netname)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    print('island vias:', fix_islands(b))
    print('orphan stitch vias removed:', prune_orphan_vias(b))
    print('GND island bridges:', bridge_islands(b))
    print('tracks narrowed around GND islands:', relax_near_islands(b))
    print('duplicate vias removed:', dedupe_vias(b))
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)
    restore_pro()
    print('fixup routed %d connection(s)' % routed)


main()
