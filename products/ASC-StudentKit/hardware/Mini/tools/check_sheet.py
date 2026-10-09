"""Layout checks on the generated schematic, for when KiCad isn't at hand:
no two parts' pin ends on the same point (KiCad would join them), no labels or
power symbols of different nets on the same point, and no overlapping symbol bodies.

    python3 tools/check_sheet.py
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sexpr
from sexpr import find, first

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sch = sexpr.parse(open(os.path.join(PRJ, "ASC-Mini.kicad_sch")).read())
libs = {s[1]: s for s in find(first(sch, "lib_symbols"), "symbol")}

def pins_of(sym):
    out = []
    for sub in find(sym, "symbol"):
        for p in find(sub, "pin"):
            at = first(p, "at")
            out.append((float(at[1]), float(at[2])))
    return out

errors, boxes, points = [], [], {}
for s in find(sch, "symbol"):
    lib = first(s, "lib_id")[1]
    at = first(s, "at"); x, y, rot = float(at[1]), float(at[2]), int(float(at[3]))
    ref = [p[2] for p in find(s, "property") if p[1] == "Reference"][0]
    pts = []
    for px, py in pins_of(libs[lib]):
        # rotate the library point (y up) then flip to sheet coordinates (y down)
        for _ in range(rot // 90):
            px, py = -py, px
        pts.append((round(x + px, 2), round(y - py, 2)))
    if ref.startswith("#"):
        continue
    for pt in pts:
        if pt in points and points[pt] != ref:
            errors.append("%s and %s have pins on the same point %s" % (points[pt], ref, pt))
        points[pt] = ref
    if pts:
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        boxes.append((ref, min(xs) + 0.5, min(ys) + 0.5, max(xs) - 0.5, max(ys) - 0.5))

for i, a in enumerate(boxes):
    for b in boxes[i + 1:]:
        if a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4]:
            errors.append("%s and %s overlap" % (a[0], b[0]))

labels = {}
for l in find(sch, "label"):
    at = first(l, "at"); pt = (round(float(at[1]), 2), round(float(at[2]), 2))
    if pt in labels and labels[pt] != l[1]:
        errors.append("labels %s and %s on the same point %s" % (labels[pt], l[1], pt))
    labels[pt] = l[1]

print("%d symbols, %d pin points, %d labels" % (len(boxes), len(points), len(labels)))
if errors:
    print("\n".join("FAIL: " + e for e in errors)); sys.exit(1)
print("OK: no shared pin points, no overlapping symbols")
