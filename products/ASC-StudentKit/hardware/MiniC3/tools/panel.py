"""Build the 96 x 100 mm fab panel: 4 x ASC-MiniC3 (48 x 44) + 4 x ASC-AHT20 sticks (48 x 6).

Runs in KiCad's python after both boards are routed:

    $KP tools/panel.py        # -> panel/ASC-MiniC3-panel.kicad_pcb

    +----------+----------+  y = 0
    |  Mini 1  |  Mini 2  |
    +----------+----------+  44   V-cut
    |  Mini 3  |  Mini 4  |
    +----------+----------+  88   V-cut
    | stick 1  | stick 2  |
    +----------+----------+  94   V-cut
    | stick 3  | stick 4  |
    +----------+----------+  100
               x = 48 V-cut, top to bottom

Every V-cut runs edge to edge, as V-scoring needs. The outline is on Edge.Cuts, the V-cut lines
on User.Comments (Cmts.User) with a "V-CUT" label: tell the fab "V-cut as per the Cmts layer".
Each copy gets its own nets (prefix B1/..B8/) and unique references (R1_1, R1_2 ...), so the
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
PW, PH = 96.0, 100.0
COPIES = [("ASC-MiniC3", 0, 0), ("ASC-MiniC3", 48, 0), ("ASC-MiniC3", 0, 44), ("ASC-MiniC3", 48, 44),
          ("ASC-AHT20", 0, 88), ("ASC-AHT20", 48, 88), ("ASC-AHT20", 0, 94), ("ASC-AHT20", 48, 94)]
VCUTS_X = [48.0]
VCUTS_Y = [44.0, 88.0, 94.0]

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

for a, b in (((0, 0), (PW, 0)), ((PW, 0), (PW, PH)), ((PW, PH), (0, PH)), ((0, PH), (0, 0))):
    line(a, b, pcbnew.Edge_Cuts, 0.05)
for x in VCUTS_X:
    line((x, -3), (x, PH + 3), pcbnew.Cmts_User, 0.2)
    text("V-CUT", x, -4.5, pcbnew.Cmts_User)
for y in VCUTS_Y:
    line((-3, y), (PW + 3, y), pcbnew.Cmts_User, 0.2)
    text("V-CUT", PW + 7, y, pcbnew.Cmts_User)
text("ASC Mini-C3 panel 96 x 100: 4 x Mini-C3 + 4 x AHT20 stick. V-cut on all Cmts.User lines, 1/3 depth each side.",
     0, PH + 6, pcbnew.Cmts_User, 1.2)

# zones keep the fills of the routed source boards (copied with them), so no refill here
path = os.path.join(OUT, "ASC-MiniC3-panel.kicad_pcb")
pcbnew.SaveBoard(path, panel)
print("panel: %d footprints, %d tracks, %d zones -> %s" % (len(panel.GetFootprints()), len(panel.GetTracks()), len(panel.Zones()), path))
