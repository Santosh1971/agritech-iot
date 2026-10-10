"""JLC-style BOM and CPL (placement) files from kicad-cli's exports in fab/.

For each board and for the panel: <name>-BOM-JLC.csv (Comment, Designator, Footprint, LCSC Part #)
and <name>-CPL-JLC.csv (Designator, Mid X, Mid Y, Layer, Rotation), SMD parts only; through-hole
parts are hand-soldered. LCSC numbers marked "verify" were not checked against the LCSC catalogue.
"""
import csv, os, re

FAB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fab")
LCSC = {"100n": "C49678", "B5819W": "C8598", "PUMP red": "C84256", "FLOW yellow": "C2296", "STATUS green": "C2297",
        "RELAY blue": "C84259 (verify)", "AO3400A": "C20917", "100": "C17408", "100k": "C149504", "1k": "C17513",
        "470": "C17710", "10k": "C17414", "4.7k": "C17673", "DS1307Z+": "C2685 (verify)", "10u": "C15850",
        "AHT20": "AHT20 (search LCSC; verify)"}

def write(name, bom_src, pos_src):
    pos = list(csv.DictReader(open(os.path.join(FAB, pos_src))))
    with open(os.path.join(FAB, name + "-CPL-JLC.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in pos:
            w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm", "Top", r["Rot"]])
    groups = {}
    for r in pos:
        groups.setdefault((r["Val"], r["Package"]), []).append(r["Ref"])
    with open(os.path.join(FAB, name + "-BOM-JLC.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, pkg), refs in sorted(groups.items()):
            refs.sort(key=lambda s: (re.sub(r"\d", "", s), [int(x) for x in re.findall(r"\d+", s)]))
            w.writerow([val, ",".join(refs), pkg, LCSC.get(val, "")])
    os.remove(os.path.join(FAB, pos_src))

write("ASC-MiniC3", "ASC-MiniC3-bom-kicad.csv", "ASC-MiniC3-pos.csv")
write("ASC-AHT20", "ASC-AHT20-bom-kicad.csv", "ASC-AHT20-pos.csv")
write("ASC-MiniC3-panel", None, "ASC-MiniC3-panel-pos.csv")
print("JLC BOM/CPL written")
