"""Build the 98 x 98 mm fab panel: 4 x ASC-MiniC3 (48 x 44) + 2 x ASC-AHT20 sticks (48 x 6),
separated by 2 mm routed slots with break-away tabs and mouse-bite holes (WhyPCB / JLC prefer
this to V-cut, which JLC charges extra for).

Runs in KiCad's python after both boards are routed:

    $KP tools/panel.py        # -> panel/ASC-MiniC3-panel.kicad_pcb

    x: 0        48 50        98
       +---------+  +---------+  y = 0
       | Mini 1  |  | Mini 2  |
       +---------+  +---------+  44
        2 mm slot (tabs at the ends and in the middle)
       +---------+  +---------+  46
       | Mini 3  |  | Mini 4  |
       +---------+  +---------+  90
       | stick 1 |  | stick 2 |  92..98
       +---------+  +---------+

The slots are closed cut-outs on Edge.Cuts. Where a slot stops, the board material bridges to its
neighbour (a tab); every tab has a row of 0.5 mm NPTH mouse-bite holes along each board edge so the
boards snap off cleanly (in the NPTH drill file). Two sticks per panel: FG1 builds do not need one.
Each copy gets its own nets (prefix B1/..B6/) and unique references (R1_1, R1_2 ...), so the
panel BOM / placement files are valid for assembly of the whole panel.
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
OUT = os.path.join(PRJ, "panel")
os.makedirs(OUT, exist_ok=True)
mm = pcbnew.FromMM
OX, OY = 100.0, 60.0
PW, PH = 98.0, 98.0
GAP = 2.0
COPIES = [("ASC-MiniC3", 0, 0), ("ASC-MiniC3", 50, 0), ("ASC-MiniC3", 0, 46), ("ASC-MiniC3", 50, 46),
          ("ASC-AHT20", 0, 92), ("ASC-AHT20", 50, 92)]
# 2 mm slots (x0, y0, x1, y1), rounded ends. Tabs are what the slots leave: 3 mm at the panel edges,
# ~7 mm around each slot crossing, and two short ones between the sticks.
SLOTS = [(48, 3, 50, 41.5), (48, 48.5, 50, 87.5), (48, 94.5, 50, 96.5),     # between the left and right columns
         (3, 44, 45.5, 46), (52.5, 44, 95, 46),                              # between the Mini rows
         (3, 90, 45.5, 92), (52.5, 90, 95, 92)]                              # between the Minis and the sticks
BITE_D, BITE_PITCH, BITE_OUT = 0.5, 0.8, 0.15   # mouse bites: hole dia, pitch, centre 0.15 mm outside the board edge

panel = pcbnew.NewBoard(os.path.join(OUT, "ASC-MiniC3-panel.kicad_pcb"))
src_boards = {}
nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(panel, name)
        panel.Add(n)
        nets[name] = n
    return nets[name]

first = True
for i, (proj, dx, dy) in enumerate(COPIES, 1):
    if proj not in src_boards:
        src_boards[proj] = pcbnew.LoadBoard(os.path.join(PRJ, proj + ".kicad_pcb"))
    src = src_boards[proj]
    if first:     # design rules from the Mini
        panel.GetDesignSettings().CloneFrom(src.GetDesignSettings()) if hasattr(panel.GetDesignSettings(), "CloneFrom") else None
        first = False
    off = pcbnew.VECTOR2I(mm(dx), mm(dy))
    tag = "B%d" % i
    def renet(item):
        n = item.GetNetname()
        if n:
            item.SetNet(net("%s%s" % (tag, n if n.startswith("/") else "/" + n)))
    for fp in src.GetFootprints():
        c = pcbnew.FOOTPRINT(fp)
        c.SetReference("%s_%d" % (fp.GetReference(), i))
        c.Move(off)
        for p in c.Pads():
            renet(p)
        panel.Add(c)
    for t in src.GetTracks():
        c = t.Duplicate().Cast()
        c.Move(off)
        renet(c)
        panel.Add(c)
    for z in src.Zones():
        c = z.Duplicate().Cast()
        c.Move(off)
        if not z.GetIsRuleArea():
            renet(c)
        panel.Add(c)
    for d in src.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts and d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            # the board's own outline becomes the panel outline / V-cuts; internal cut-outs stay
            sb = src.GetBoardEdgesBoundingBox()
            x0, y0, x1, y1 = sb.GetX(), sb.GetY(), sb.GetRight(), sb.GetBottom()
            a_, b_ = d.GetStart(), d.GetEnd()
            near = lambda u, v: abs(u - v) < mm(0.05)
            if (near(a_.x, x0) and near(b_.x, x0)) or (near(a_.x, x1) and near(b_.x, x1)) or \
               (near(a_.y, y0) and near(b_.y, y0)) or (near(a_.y, y1) and near(b_.y, y1)):
                continue
        c = d.Duplicate().Cast()
        c.Move(off)
        panel.Add(c)

def line(a, b, layer, w):
    s = pcbnew.PCB_SHAPE(panel); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(mm(OX + a[0]), mm(OY + a[1]))); s.SetEnd(pcbnew.VECTOR2I(mm(OX + b[0]), mm(OY + b[1])))
    s.SetLayer(layer); s.SetWidth(mm(w)); panel.Add(s)

def text(t, x, y, layer, size=1.5, angle=0):
    s = pcbnew.PCB_TEXT(panel)
    s.SetText(t); s.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); s.SetLayer(layer)
    s.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * 0.15)); s.SetTextAngleDegrees(angle)
    panel.Add(s)

def arc(c, start, end, mid, layer=pcbnew.Edge_Cuts, w=0.05):
    s_ = pcbnew.PCB_SHAPE(panel); s_.SetShape(pcbnew.SHAPE_T_ARC)
    P = lambda p: pcbnew.VECTOR2I(mm(OX + p[0]), mm(OY + p[1]))
    s_.SetArcGeometry(P(start), P(mid), P(end)); s_.SetLayer(layer); s_.SetWidth(mm(w)); panel.Add(s_)

for a, b in (((0, 0), (PW, 0)), ((PW, 0), (PW, PH)), ((PW, PH), (0, PH)), ((0, PH), (0, 0))):
    line(a, b, pcbnew.Edge_Cuts, 0.05)
for x0, y0, x1, y1 in SLOTS:                    # stadium-shaped slot outlines
    r = min(x1 - x0, y1 - y0) / 2
    if abs((x1 - x0) - (y1 - y0)) < 0.01:       # as long as it is wide: a round hole
        c = pcbnew.PCB_SHAPE(panel); c.SetShape(pcbnew.SHAPE_T_CIRCLE)
        c.SetCenter(pcbnew.VECTOR2I(mm(OX + (x0 + x1) / 2), mm(OY + (y0 + y1) / 2)))
        c.SetEnd(pcbnew.VECTOR2I(mm(OX + x1), mm(OY + (y0 + y1) / 2)))
        c.SetLayer(pcbnew.Edge_Cuts); c.SetWidth(mm(0.05)); panel.Add(c)
    elif (x1 - x0) <= (y1 - y0):                # vertical slot
        cx = (x0 + x1) / 2
        line((x0, y0 + r), (x0, y1 - r), pcbnew.Edge_Cuts, 0.05); line((x1, y0 + r), (x1, y1 - r), pcbnew.Edge_Cuts, 0.05)
        arc(None, (x0, y0 + r), (x1, y0 + r), (cx, y0)); arc(None, (x1, y1 - r), (x0, y1 - r), (cx, y1))
    else:                                       # horizontal slot
        cy = (y0 + y1) / 2
        line((x0 + r, y0), (x1 - r, y0), pcbnew.Edge_Cuts, 0.05); line((x0 + r, y1), (x1 - r, y1), pcbnew.Edge_Cuts, 0.05)
        arc(None, (x0 + r, y0), (x0 + r, y1), (x0, cy)); arc(None, (x1 - r, y1), (x1 - r, y0), (x1, cy))

# mouse bites: along every board edge that touches a tab (not the panel border, not a slot)
def in_slot(x, y):
    return any(x0 - 0.05 <= x <= x1 + 0.05 and y0 - 0.05 <= y <= y1 + 0.05 for x0, y0, x1, y1 in SLOTS)
def bite_fp(x, y, n):
    fp = pcbnew.FOOTPRINT(panel)
    fp.SetReference("MB%d" % n); fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
    fp.SetAttributes(pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_BOARD_ONLY)
    p = pcbnew.PAD(fp); p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH); p.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
    p.SetSize(pcbnew.VECTOR2I(mm(BITE_D), mm(BITE_D))); p.SetDrillSize(pcbnew.VECTOR2I(mm(BITE_D), mm(BITE_D)))
    p.SetLayerSet(pcbnew.PAD.UnplatedHoleMask()); fp.Add(p)
    fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); panel.Add(fp)
nb = 0
for proj, dx, dy in COPIES:
    w, h = {"ASC-MiniC3": (48.0, 44.0), "ASC-AHT20": (48.0, 6.0)}[proj]
    edges = [((dx, dy), (dx + w, dy), (0, -1)), ((dx, dy + h), (dx + w, dy + h), (0, 1)),
             ((dx, dy), (dx, dy + h), (-1, 0)), ((dx + w, dy), (dx + w, dy + h), (1, 0))]
    for (ax, ay), (bx, by), (nx, ny) in edges:
        if (nx and (abs(ax) < 0.01 and nx < 0 or abs(ax - PW) < 0.01 and nx > 0)) or (ny and (abs(ay) < 0.01 and ny < 0 or abs(ay - PH) < 0.01 and ny > 0)):
            continue                            # panel border: nothing to break
        L = abs(bx - ax) + abs(by - ay)
        k = int(L / BITE_PITCH)
        for i in range(k + 1):
            t = (0.5 + i * BITE_PITCH) if L > 1 else L / 2
            if t > L - 0.5:
                break
            x = ax + (bx - ax) * t / L + nx * BITE_OUT
            y = ay + (by - ay) * t / L + ny * BITE_OUT
            if in_slot(ax + (bx - ax) * t / L + nx * 1.0, ay + (by - ay) * t / L + ny * 1.0):
                continue                        # a slot is on the other side: not a tab
            nb += 1
            bite_fp(x, y, nb)

text("ASC Mini-C3 panel 98 x 98: 4 x Mini-C3 + 2 x AHT20 stick. 2 mm routed slots (Edge.Cuts), "
     "tabs with 0.5 mm mouse-bite holes (NPTH drill). No V-cut.", 0, PH + 6, pcbnew.Cmts_User, 1.2)

# zones keep the fills of the routed source boards (copied with them), so no refill here
path = os.path.join(OUT, "ASC-MiniC3-panel.kicad_pcb")
import apply_3d
apply_3d.set_models(panel, "${KIPRJMOD}/../ASC.3dshapes/")
pcbnew.SaveBoard(path, panel)
print("mouse-bite holes: %d" % nb)
print("panel: %d footprints, %d tracks, %d zones -> %s" % (len(panel.GetFootprints()), len(panel.GetTracks()), len(panel.Zones()), path))
