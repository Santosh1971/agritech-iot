"""Read KiCad footprints (.kicad_mod) for gen_pcb.py: geometry, pads and courtyard."""
import math, os
import sexpr
from sexpr import find, first

FPROOT = os.environ.get("KICAD_FOOTPRINT_DIR", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints").rstrip("/") + "/"
PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def load(fpid):
    lib, name = fpid.split(":")
    base = os.path.join(PRJ, lib + ".pretty") if lib == "ASC" else FPROOT + lib + ".pretty"
    return sexpr.parse(open(os.path.join(base, name + ".kicad_mod")).read())

def _pts(item):
    out = []
    for k in ("start", "end", "mid", "center", "at"):
        for e in find(item, k):
            out.append((float(e[1]), float(e[2])))
    pts = first(item, "pts")
    if pts:
        out += [(float(p[1]), float(p[2])) for p in find(pts, "xy")]
    if item[0] == "fp_circle":
        c = first(item, "center"); e = first(item, "end")
        r = math.hypot(float(e[1]) - float(c[1]), float(e[2]) - float(c[2]))
        out += [(float(c[1]) - r, float(c[2]) - r), (float(c[1]) + r, float(c[2]) + r)]
    return out

def courtyard(fp):
    """Local bounding box (x0, y0, x1, y1) of the courtyard, or of the pads if none."""
    pts = []
    for kind in ("fp_line", "fp_rect", "fp_poly", "fp_circle", "fp_arc"):
        for it in find(fp, kind):
            lay = first(it, "layer")
            if lay and lay[1].endswith("CrtYd"):
                pts += _pts(it)
    if not pts:
        for p in find(fp, "pad"):
            at = first(p, "at"); sz = first(p, "size")
            x, y, w, h = float(at[1]), float(at[2]), float(sz[1]), float(sz[2])
            pts += [(x - w / 2, y - h / 2), (x + w / 2, y + h / 2)]
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)

def rot(x, y, deg):
    """KiCad rotation: positive is counter-clockwise on screen (y points down)."""
    a = math.radians(deg)
    return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)

def placed_box(fp, x, y, deg, back=False):
    x0, y0, x1, y1 = courtyard(fp)
    if back:
        x0, x1 = -x1, -x0
    corners = [rot(px, py, deg) for px, py in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    xs = [x + c[0] for c in corners]; ys = [y + c[1] for c in corners]
    return min(xs), min(ys), max(xs), max(ys)
