"""Build the 99 x 98 mm fab panel: 2 x WM-Mini C3 (99 x 48) separated by a 2 mm routed slot with plain
break-away tabs (no V-cut, no mouse bites), as the ASC Mini-C3 panel. Runs in KiCad's python after routing:

    $KP tools/panel.py        # -> panel/WM-MiniC3-panel.kicad_pcb

    y = 0   +---------------------------+   board 1, turned 180 deg: its connector edge is the panel's top edge
            |   WM-Mini C3 #1 (rot 180)  |
       48   +----  2 mm slot, 3 tabs ----+
       50   +---------------------------+
            |   WM-Mini C3 #2            |   board 2: connector edge = the panel's bottom edge
       98   +---------------------------+

Both connector edges face outwards, so the pluggable headers (their plugs overhang the board edge) never
sit over the other board. Each copy gets its own nets (B1/, B2/) and references (R1_1, R1_2 ...).
Solder the headers and relays after breaking the panel if they get in the way.
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
OUT = os.path.join(PRJ, "panel")
os.makedirs(OUT, exist_ok=True)
mm = pcbnew.FromMM
OX, OY = 100.0, 60.0                         # the source board's top-left corner on its page
PW, PH, BW, BH = 99.0, 98.0, 99.0, 48.0
# 2 mm slot between the boards, stopping 2 mm short of each panel edge, with a 5 mm tab in the middle
SLOTS = [(2.0, 48.0, 47.0, 50.0), (52.0, 48.0, 97.0, 50.0)]

panel = pcbnew.NewBoard(os.path.join(OUT, "WM-MiniC3-panel.kicad_pcb"))
src = pcbnew.LoadBoard(os.path.join(PRJ, "WM-MiniC3.kicad_pcb"))
panel.GetDesignSettings().CloneFrom(src.GetDesignSettings()) if hasattr(panel.GetDesignSettings(), "CloneFrom") else None
nets = {}
def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(panel, name)
        panel.Add(n)
        nets[name] = n
    return nets[name]

centre = pcbnew.VECTOR2I(mm(OX + BW / 2), mm(OY + BH / 2))
for i, (rot, dy) in enumerate(((180, 0.0), (0, 50.0)), 1):
    tag = "B%d" % i
    off = pcbnew.VECTOR2I(0, mm(dy))
    def place(item):
        if rot:
            item.Rotate(centre, pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
        item.Move(off)
    def renet(item):
        n = item.GetNetname()
        if n:
            item.SetNet(net("%s%s" % (tag, n if n.startswith("/") else "/" + n)))
    for fp in src.GetFootprints():
        c = pcbnew.FOOTPRINT(fp)
        c.SetReference("%s_%d" % (fp.GetReference(), i))
        place(c)
        for p in c.Pads():
            renet(p)
        panel.Add(c)
    for t in src.GetTracks():
        c = t.Duplicate().Cast()
        place(c); renet(c)
        panel.Add(c)
    for z in src.Zones():
        c = z.Duplicate().Cast()
        place(c)
        if not z.GetIsRuleArea():
            renet(c)
        panel.Add(c)
    for d in src.GetDrawings():
        if d.GetLayer() == pcbnew.Edge_Cuts:
            continue                               # the board outline becomes the panel outline + slot
        c = d.Duplicate().Cast()
        place(c)
        panel.Add(c)

def line(a, b, layer=pcbnew.Edge_Cuts, w=0.05):
    s = pcbnew.PCB_SHAPE(panel); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(mm(OX + a[0]), mm(OY + a[1]))); s.SetEnd(pcbnew.VECTOR2I(mm(OX + b[0]), mm(OY + b[1])))
    s.SetLayer(layer); s.SetWidth(mm(w)); panel.Add(s)
def arc(start, mid, end):
    s = pcbnew.PCB_SHAPE(panel); s.SetShape(pcbnew.SHAPE_T_ARC)
    P = lambda p: pcbnew.VECTOR2I(mm(OX + p[0]), mm(OY + p[1]))
    s.SetArcGeometry(P(start), P(mid), P(end)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.05)); panel.Add(s)
def text(t, x, y, size=1.2):
    s = pcbnew.PCB_TEXT(panel)
    s.SetText(t); s.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); s.SetLayer(pcbnew.Cmts_User)
    s.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * 0.15)); panel.Add(s)

for a, b in (((0, 0), (PW, 0)), ((PW, 0), (PW, PH)), ((PW, PH), (0, PH)), ((0, PH), (0, 0))):
    line(a, b)
for x0, y0, x1, y1 in SLOTS:                      # stadium-shaped horizontal slots
    r = (y1 - y0) / 2; cy = (y0 + y1) / 2
    line((x0 + r, y0), (x1 - r, y0)); line((x0 + r, y1), (x1 - r, y1))
    arc((x0 + r, y0), (x0, cy), (x0 + r, y1)); arc((x1 - r, y1), (x1, cy), (x1 - r, y0))
text("WM-Mini C3 panel 99 x 98: 2 boards, 2 mm routed slot (Edge.Cuts), plain break-away tabs. No V-cut, no mouse bites.", 0, PH + 6)

import apply_3d
apply_3d.set_models(panel, "${KIPRJMOD}/../ASC.3dshapes/")
path = os.path.join(OUT, "WM-MiniC3-panel.kicad_pcb")
pcbnew.SaveBoard(path, panel)
print("panel: %d footprints, %d tracks, %d zones -> %s" % (len(panel.GetFootprints()), len(panel.GetTracks()), len(panel.Zones()), path))
