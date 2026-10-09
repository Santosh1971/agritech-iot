"""Carry the routing of an old board over to a re-placed one (gen_pcb.py output).

Every track and via of OLD is copied into NEW, except those that come within 0.5 mm of a part
whose position changed (old or new place); nothing else moved, so nothing else can clash. Then
patch_route.py reconnects whatever that leaves split.

    python3 tools/carry_routing.py OLD.kicad_pcb        (NEW is ASC-Mini.kicad_pcb, written by gen_pcb.py)
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first, dump
import design as D

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEW = os.path.join(PRJ, D.PROJECT + ".kicad_pcb")
old = sexpr.parse(open(sys.argv[1]).read())
new = sexpr.parse(open(NEW).read())

def rot(x, y, d):
    a = math.radians(d); return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def fp_map(b):
    out = {}
    for f in find(b, "footprint"):
        ref = [p[2] for p in find(f, "property") if p[1] == "Reference"][0]
        out[ref] = f
    return out

def pads_of(f):
    at = first(f, "at"); fx, fy, fr = float(at[1]), float(at[2]), float(at[3]) if len(at) > 3 else 0.0
    out = []
    for p in find(f, "pad"):
        pa, sz = first(p, "at"), first(p, "size")
        dx, dy = rot(float(pa[1]), float(pa[2]), fr)
        lay = first(p, "layers")[1:]
        on = {"F.Cu", "B.Cu"} if (p[2] in ("thru_hole", "np_thru_hole") or "*.Cu" in lay) else {l for l in lay if l.endswith(".Cu")}
        n = first(p, "net")
        out.append(dict(x=fx + dx, y=fy + dy, r=math.hypot(float(sz[1]), float(sz[2])) / 2, layers=on, net=n[2] if n else ""))
    return out

of, nf = fp_map(old), fp_map(new)
moved = [r for r in nf if r in of and (first(of[r], "at")[1:] != first(nf[r], "at")[1:] or first(of[r], "layer")[1] != first(nf[r], "layer")[1])]
zones = []                                   # areas around moved parts, old and new place
for r in moved:
    for f in (of[r], nf[r]):
        for p in pads_of(f):
            zones.append((p["x"], p["y"], p["r"] + 0.5))
new_pads = [p for f in nf.values() for p in pads_of(f)]
old_nets = {int(n[1]): n[2] for n in find(old, "net")}
new_ids = {n[2]: int(n[1]) for n in find(new, "net")}

def near(x, y, r):
    return any(math.hypot(x - zx, y - zy) < zr + r for zx, zy, zr in zones)

def touches_other(x, y, r, layers, net):
    return any(p["net"] != net and p["layers"] & layers and math.hypot(x - p["x"], y - p["y"]) < p["r"] + r for p in new_pads)

kept = dropped = 0
tail = new.pop()
for e in old:
    if not (isinstance(e, list) and e and e[0] in ("segment", "via")):
        continue
    net = old_nets[int(first(e, "net")[1])]
    if e[0] == "segment":
        st, en = first(e, "start"), first(e, "end")
        pts = [(float(st[1]) + (float(en[1]) - float(st[1])) * k / 8, float(st[2]) + (float(en[2]) - float(st[2])) * k / 8) for k in range(9)]
        r = float(first(e, "width")[1]) / 2; layers = {first(e, "layer")[1]}
    else:
        at = first(e, "at"); pts = [(float(at[1]), float(at[2]))]; r = float(first(e, "size")[1]) / 2; layers = {"F.Cu", "B.Cu"}
    if any(near(x, y, r) for x, y in pts):          # only the moved parts' surroundings change
        dropped += 1; continue
    first(e, "net")[1] = new_ids[net]
    new.append(e); kept += 1
new.append(tail)
open(NEW, "w").write(dump(new) + "\n")
print("moved parts: %s; kept %d tracks/vias, dropped %d" % (", ".join(moved) or "none", kept, dropped))
