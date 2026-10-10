"""Route <PROJECT>.kicad_pcb (DESIGN picks the design module) with Freerouting, then fill the zones. Runs in KiCad's python:

    FREEROUTING_JAR=~/Applications/freerouting/freerouting-1.9.0.jar \
    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 tools/route.py

  1. net classes from D.NETCLASSES (Power 0.5 mm; Mains 1.5 mm, kept 5 mm from everything);
     everything else 0.3 mm
  2. export route/<PROJECT>.dsn, run Freerouting headless, import route/<PROJECT>.ses
  3. fill the GND pours on both layers and save

Re-run gen_pcb.py first: this starts from an unrouted board.
"""
import importlib, json, os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
sys.path.insert(0, HERE)
D = importlib.import_module(os.environ.get("DESIGN", "design"))

PCB = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
OUT = os.path.join(PRJ, "route")
os.makedirs(OUT, exist_ok=True)
DSN, SES = os.path.join(OUT, D.PROJECT + ".dsn"), os.path.join(OUT, D.PROJECT + ".ses")
mm = pcbnew.FromMM

board = pcbnew.LoadBoard(PCB)
ns = board.GetDesignSettings().m_NetSettings
def set_classes(route):
    """route=True: the class clearances Freerouting needs (mains 5 mm from everything).
    route=False: the saved classes; mains-to-other clearance then comes from the .kicad_dru rule."""
    for name, (w, clr, members) in D.NETCLASSES.items():
        c = pcbnew.NETCLASS(name)
        c.SetTrackWidth(mm(w)); c.SetClearance(mm(clr if route else min(clr, 0.2)))
        c.SetViaDiameter(mm(0.7)); c.SetViaDrill(mm(0.35))
        ns.SetNetclass(name, c)
        for n in members:
            ns.SetNetclassPatternAssignment(n, name)
    ns.RecomputeEffectiveNetclasses()
    board.SynchronizeNetsAndNetClasses(True)
set_classes(True)

# GND is routed as tracks too (the pours alone left pads islanded): take the pours out for the
# export so Freerouting does not treat GND as a plane, and put them back before filling.
# The antenna rule area only bans pour, but the DSN export turns it into a full keep-out that
# walls off the module's IO0/IO1 pads, so it is taken out for the export as well. Real
# no-track areas (D.NO_TRACK) stay in.
pours = [z for z in board.Zones() if not (z.GetIsRuleArea() and z.GetDoNotAllowTracks())]
for z in pours:
    board.Remove(z)
if not pcbnew.ExportSpecctraDSN(board, DSN):
    sys.exit("DSN export failed")
jar = os.path.expanduser(os.environ.get("FREEROUTING_JAR", "~/Applications/freerouting/freerouting-1.9.0.jar"))
if os.path.exists(SES):
    os.remove(SES)
subprocess.run([os.environ.get("JAVA", "java"), "-jar", jar, "-de", DSN, "-do", SES, "-mp", os.environ.get("ROUTE_PASSES", "100")],
               check=False, cwd=OUT, timeout=900)
if not os.path.exists(SES):
    sys.exit("Freerouting wrote no session file")
if not pcbnew.ImportSpecctraSES(board, SES):
    sys.exit("SES import failed")

for z in pours:
    board.Add(z)
set_classes(False)
filler = pcbnew.ZONE_FILLER(board)
filler.Fill(board.Zones())
pcbnew.SaveBoard(PCB, board)
# The net classes live in the project file; write them there so kicad-cli's DRC sees them.
pro = os.path.join(PRJ, D.PROJECT + ".kicad_pro")
cfg = json.load(open(pro))
nsj = cfg.setdefault("net_settings", {})
classes = [c for c in nsj.get("classes", []) if c.get("name") == "Default"]
for name, (w, clr, members) in D.NETCLASSES.items():
    classes.append({"name": name, "track_width": w, "clearance": min(clr, 0.2), "via_diameter": 0.7, "via_drill": 0.35,
                    "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 0,
                    "bus_width": 12, "wire_width": 6, "line_style": 0, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25,
                    "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1})
nsj["classes"] = classes
nsj["netclass_patterns"] = [{"netclass": name, "pattern": n} for name, (w, c, members) in D.NETCLASSES.items() for n in members]
json.dump(cfg, open(pro, "w"), indent=2)
tracks = [t for t in board.GetTracks()]
print("routed: %d tracks, %d vias" % (sum(1 for t in tracks if t.GetClass() == "PCB_TRACK"),
                                      sum(1 for t in tracks if t.GetClass() == "PCB_VIA")))
