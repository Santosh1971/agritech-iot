"""Generate the WM-Mini C3 schematic (copied from ASC Mini-C3's generator) and tools/netlist.json from design.py.

Adapted from AWD1's generator (products/AWD1-paddy/hardware/AWD-FieldNode/tools):
every pin connects by a net label or power symbol placed on its endpoint, so the
schematic has no hand-drawn wires. Run with any python3:

    KICAD_SYMBOL_DIR=/path/to/kicad/symbols python3 tools/gen_sch.py

KICAD_SYMBOL_DIR defaults to KiCad 9 on macOS. Then open ASC-MiniC3.kicad_sch in
KiCad 9 and run ERC.
"""
import copy, json, os, re, uuid
import sexpr
from sexpr import Sym, find, first, dump
import importlib
D = importlib.import_module(os.environ.get("DESIGN", "design"))

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
KLIB = os.environ.get("KICAD_SYMBOL_DIR", "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols").rstrip("/") + "/"

def uid():
    return str(uuid.uuid4())

# ---- symbol libraries ---------------------------------------------------------
_libs = {}
def lib_tree(lib):
    if lib not in _libs:
        _libs[lib] = sexpr.parse(open(KLIB + lib + ".kicad_sym").read())
    return _libs[lib]

def raw_symbol(lib, name):
    for s in find(lib_tree(lib), "symbol"):
        if s[1] == name:
            return s
    raise KeyError(lib + ":" + name)

def flat_symbol(lib, name):
    """Resolve (extends ...) into a stand-alone symbol named `name`."""
    s = copy.deepcopy(raw_symbol(lib, name))
    ext = first(s, "extends")
    if not ext:
        return s
    parent = flat_symbol(lib, ext[1])
    out = [Sym("symbol"), name]
    child_props = {p[1]: p for p in find(s, "property")}
    for e in parent[2:]:
        if isinstance(e, list) and e[0] == "property":
            out.append(child_props.pop(e[1], e))
        elif isinstance(e, list) and e[0] == "symbol":
            e = copy.deepcopy(e)
            e[1] = name + e[1][len(parent[1]):]
            out.append(e)
        else:
            out.append(e)
    out[2:2] = list(child_props.values())
    return out

def supermini():
    """ESP32-C3 Super Mini as one 16-pin symbol, pins grouped by function (WM-Mini C3): power on top and
    bottom, relay drives and flow on the right, I2C and strapping pins on the left. Pad numbers follow
    ASC.pretty/ESP32-C3_SuperMini_Socket: 1-8 = IO5 IO6 IO7 IO8 IO9 IO10 IO20 IO21, 9-16 = 5V GND 3V3 IO4 IO3 IO2 IO1 IO0."""
    num = {n: str(i + 1) for i, n in enumerate(["IO5", "IO6", "IO7", "IO8", "IO9", "IO10", "IO20", "IO21",
                                                 "5V", "GND", "3V3", "IO4", "IO3", "IO2", "IO1", "IO0"])}
    eff = lambda: [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]
    def pin(name, x, y, ang):
        kind = {"5V": "power_out", "3V3": "power_out", "GND": "power_in"}.get(name, "bidirectional")
        return [Sym("pin"), Sym(kind), Sym("line"), [Sym("at"), x, y, ang], [Sym("length"), 3.81],
                [Sym("name"), name, eff()], [Sym("number"), num[name], eff()]]
    pins = [pin("5V", -2.54, 16.51, 270), pin("3V3", 2.54, 16.51, 270), pin("GND", 0, -16.51, 90)]
    for n, y in (("IO20", 10.16), ("IO3", 7.62), ("IO4", 5.08), ("IO10", 2.54), ("IO0", 0), ("IO1", -2.54),
                 ("IO5", -7.62), ("IO21", -10.16)):
        pins.append(pin(n, 13.97, y, 180))
    for n, y in (("IO6", 10.16), ("IO7", 7.62), ("IO2", 2.54), ("IO8", -7.62), ("IO9", -10.16)):
        pins.append(pin(n, -13.97, y, 0))
    def p(name, val, y, hide=False):
        e = eff()
        if hide:
            e.append([Sym("hide"), Sym("yes")])
        return [Sym("property"), name, val, [Sym("at"), 0, y, 0], e]
    return [Sym("symbol"), "ESP32-C3_SuperMini", [Sym("pin_names"), [Sym("offset"), 1.016]],
            [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")],
            p("Reference", "U", 19.05), p("Value", "ESP32-C3_SuperMini", -19.05),
            p("Footprint", "ASC:ESP32-C3_SuperMini_Socket", -21.59, True),
            p("Datasheet", "https://www.nologo.tech/en/product/esp32/esp32c3/esp32c3supermini/esp32C3SuperMini.html", -24.13, True),
            p("Description", "ESP32-C3 Super Mini plug-in board (USB-C, 3.3 V LDO), 2 x 8 pins", -26.67, True),
            [Sym("symbol"), "ESP32-C3_SuperMini_0_1",
             [Sym("rectangle"), [Sym("start"), -10.16, 12.7], [Sym("end"), 10.16, -12.7],
              [Sym("stroke"), [Sym("width"), 0.254], [Sym("type"), Sym("default")]], [Sym("fill"), [Sym("type"), Sym("background")]]]],
            [Sym("symbol"), "ESP32-C3_SuperMini_1_1"] + pins]

def hf46f():
    """Hongfa HF46F (1 Form A): Omron's G5Q-1A symbol, renumbered to the HF46F pads
    (1/2 coil, 3/4 contact; see ASC.pretty/Relay_SPST_Hongfa_HF46F)."""
    s = flat_symbol("Relay", "G5Q-1A")
    s[1] = "HF46F"
    renum = {"5": "1", "1": "2", "3": "3", "2": "4"}
    for sub in find(s, "symbol"):
        sub[1] = sub[1].replace("G5Q-1A", "HF46F")
        for p in find(sub, "pin"):
            num = first(p, "number")
            num[1] = renum[num[1]]
    for pr in find(s, "property"):
        if pr[1] == "Value":
            pr[2] = "HF46F"
        elif pr[1] == "Footprint":
            pr[2] = "ASC:Relay_SPST_Hongfa_HF46F"
        elif pr[1] == "Datasheet":
            pr[2] = "https://www.hongfa.com/Product/Item/HF46F"
        elif pr[1] == "Description":
            pr[2] = "Hongfa HF46F subminiature power relay, SPST-NO (1 Form A), 5 A"
        elif pr[1] == "ki_fp_filters":
            pr[2] = "Relay*SPST*Hongfa*HF46F*"
    return s

def aht20():
    """ASAIR AHT20 (DFN-6, 3 x 3 mm). Pin numbers from the datasheet's table 5."""
    eff = lambda: [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]
    def pin(num, name, x, y, ang, kind):
        return [Sym("pin"), Sym(kind), Sym("line"), [Sym("at"), x, y, ang], [Sym("length"), 2.54],
                [Sym("name"), name, eff()], [Sym("number"), num, eff()]]
    pins = [pin("2", "VDD", 0, 7.62, 270, "power_in"), pin("5", "GND", 0, -7.62, 90, "power_in"),
            pin("3", "SCL", -10.16, 0, 0, "input"), pin("4", "SDA", -10.16, -2.54, 0, "bidirectional"),
            pin("1", "NC", 10.16, 0, 180, "no_connect"), pin("6", "NC", 10.16, -2.54, 180, "no_connect")]
    def p(name, val, y, hide=False):
        e = eff()
        if hide:
            e.append([Sym("hide"), Sym("yes")])
        return [Sym("property"), name, val, [Sym("at"), 0, y, 0], e]
    return [Sym("symbol"), "AHT20", [Sym("pin_names"), [Sym("offset"), 1.016]],
            [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")],
            p("Reference", "U", 10.16), p("Value", "AHT20", -10.16), p("Footprint", "ASC:Aosong_AHT20_DFN-6_3x3mm", -12.7, True),
            p("Datasheet", "https://asairsensors.com/wp-content/uploads/2021/09/Data-Sheet-AHT20-Humidity-and-Temperature-Sensor-ASAIR-V1.0.03.pdf", -15.24, True),
            p("Description", "ASAIR AHT20 I2C humidity and temperature sensor, 0x38", -17.78, True),
            [Sym("symbol"), "AHT20_0_1",
             [Sym("rectangle"), [Sym("start"), -7.62, 5.08], [Sym("end"), 7.62, -5.08],
              [Sym("stroke"), [Sym("width"), 0.254], [Sym("type"), Sym("default")]], [Sym("fill"), [Sym("type"), Sym("background")]]]],
            [Sym("symbol"), "AHT20_1_1"] + pins]

ASC_SYMBOLS = {"ESP32-C3_SuperMini": supermini}

def get_symbol(lib_id):
    lib, name = lib_id.split(":")
    return ASC_SYMBOLS[name]() if lib == "ASC" else flat_symbol(lib, name)

def sym_pins(sym):
    """[(number, name, x, y, angle, hidden)] for unit 1 / body style 1 (and unit 0)."""
    pins = []
    for sub in find(sym, "symbol"):
        m = re.search(r"_(\d+)_(\d+)$", sub[1])
        if not m or m.group(2) != "1" or m.group(1) not in ("0", "1"):
            continue
        for p in find(sub, "pin"):
            at = first(p, "at")
            hid = any(isinstance(e, list) and e[0] == "hide" for e in p) or Sym("hide") in p
            pins.append((first(p, "number")[1], first(p, "name")[1], float(at[1]), float(at[2]), int(float(at[3])), hid))
    return pins

def resolve(pins, key):
    nums = [p for p in pins if p[0] == key]
    if nums:
        return nums
    hits = [p for p in pins if p[1] == key or key in p[1].split("/")]
    if not hits:
        raise KeyError(key)
    return hits

# ---- schematic items ------------------------------------------------------------
ROOT = uid()
items, lib_syms, pwr_n = [], {}, [0]
font = [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]]]

def prop(name, val, x, y, hide=False, justify=None, ang=0):
    eff = copy.deepcopy(font)
    if justify:
        eff.append([Sym("justify")] + [Sym(j) for j in justify.split()])
    if hide:
        eff.append([Sym("hide"), Sym("yes")])
    return [Sym("property"), name, val, [Sym("at"), x, y, ang], eff]

def place_symbol(lib_id, sym, ref, value, fp, x, y, rot=0, in_bom=True, desc_xy=None, extra=None, dnp=False):
    lib_syms.setdefault(lib_id, sym)
    s = [Sym("symbol"), [Sym("lib_id"), lib_id], [Sym("at"), x, y, rot], [Sym("unit"), 1],
         [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes" if in_bom else "no")],
         [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("yes" if dnp else "no")], [Sym("uuid"), uid()]]
    power = ref.startswith("#")
    rx, ry = desc_xy or (x + 3.81, y - 1.27)
    fa = 90 if rot % 180 == 90 else 0           # field angles are stored relative to the symbol: keep text horizontal
    s.append(prop("Reference", ref, rx, ry, hide=power, justify="left", ang=fa))
    s.append(prop("Value", value, rx, ry + 2.54, hide=False, justify="left", ang=fa))
    s.append(prop("Footprint", fp, x, y, hide=True))
    s.append(prop("Datasheet", "", x, y, hide=True))
    for k, v in (extra or {}).items():
        s.append(prop(k, v, x, y, hide=True))
    for num in sorted({p[0] for p in sym_pins(sym)}):
        s.append([Sym("pin"), num, [Sym("uuid"), uid()]])
    s.append([Sym("instances"), [Sym("project"), D.PROJECT,
             [Sym("path"), "/" + ROOT, [Sym("reference"), ref], [Sym("unit"), 1]]]])
    items.append(s)
    return s[8][1]

def rot_xy(px, py, rot):
    """A symbol point rotated counter-clockwise by rot (symbol space, y up)."""
    return {0: (px, py), 90: (-py, px), 180: (-px, -py), 270: (py, -px)}[rot % 360]

def pin_end(x, y, p, rot=0):
    px, py = rot_xy(p[2], p[3], rot)
    return round(x + px, 2), round(y - py, 2)

# label direction: lib angle 0 -> connection on the left, so the label points left (180)
LABEL_DIR = {0: 180, 180: 0, 270: 90, 90: 270}
UP = {270: 0, 90: 180, 0: 90, 180: 270}       # +3V3 / +5V point away from the pin
POWER_ROT = {"GND": {90: 0, 270: 180, 0: 270, 180: 90}, "+3V3": UP, "+5V": UP}

def label(net, x, y, ang):
    just = "left bottom" if ang in (0, 90) else "right bottom"
    eff = copy.deepcopy(font)
    eff.append([Sym("justify")] + [Sym(j) for j in just.split()])
    items.append([Sym("label"), net, [Sym("at"), x, y, ang], [Sym("fields_autoplaced"), Sym("yes")], eff, [Sym("uuid"), uid()]])

def power(net, x, y, rot):
    pwr_n[0] += 1
    place_symbol("power:" + net, flat_symbol("power", net), "#PWR%02d" % pwr_n[0], net, "", x, y, rot, in_bom=False,
                 desc_xy=(x, y + (5.08 if net == "GND" and rot == 0 else -5.08)))

def no_connect(x, y):
    items.append([Sym("no_connect"), [Sym("at"), x, y], [Sym("uuid"), uid()]])

def text(t, x, y, size=1.27):
    items.append([Sym("text"), t, [Sym("exclude_from_sim"), Sym("no")], [Sym("at"), x, y, 0],
                  [Sym("effects"), [Sym("font"), [Sym("size"), size, size]], [Sym("justify"), Sym("left"), Sym("bottom")]],
                  [Sym("uuid"), uid()]])

# ---- wires (WM-Mini C3): polylines between pin ends and fixed points; wired pins get no automatic label
POS = {}
for p in D.P:
    POS[p["ref"]] = (p["xy"][0], p["xy"][1], p.get("rot", 0), sym_pins(get_symbol(p["lib"])))
def point(pt):
    if isinstance(pt[0], str):
        x, y, rot, pins = POS[pt[0]]
        return pin_end(x, y, resolve(pins, pt[1])[0], rot)
    return (round(pt[0], 2), round(pt[1], 2))
WIRED = set()
SEGS = []
for poly in getattr(D, "WIRES", []):
    pts = [point(q) for q in poly]
    for q in poly:
        if isinstance(q[0], str):
            x, y, rot, pins = POS[q[0]]
            WIRED.add((q[0], resolve(pins, q[1])[0][0]))
    for a, b in zip(pts, pts[1:]):
        if a != b:
            SEGS.append((a, b))
for ref, key in getattr(D, "NOLABEL", []):
    WIRED.add((ref, resolve(POS[ref][3], key)[0][0]))

netlist = []
for p in D.P:
    sym = get_symbol(p["lib"])
    x, y = p["xy"]
    rot = p.get("rot", 0)
    pins = sym_pins(sym)
    desc = p.get("desc") or ((x - 10.16, y - 21.59) if p["lib"] == "ASC:ESP32-C3_SuperMini" else None)
    in_bom = not p["ref"].startswith(("H", "TP")) and not p.get("dnp")
    extra = {}
    if p.get("silk"):
        extra["Silk"] = p["silk"]
    if p.get("side"):
        extra["Side"] = p["side"]
    suuid = place_symbol(p["lib"], sym, p["ref"], p["value"], p["fp"], x, y, rot, desc_xy=desc, in_bom=in_bom, extra=extra, dnp=p.get("dnp", False))
    padnets, done_pts = {}, set()
    for key, net in p["pins"].items():
        for pin in resolve(pins, key):
            padnets[pin[0]] = D.netname(net)
            pt = pin_end(x, y, pin, rot)
            if pt in done_pts:
                continue
            done_pts.add(pt)
            if (p["ref"], pin[0]) in WIRED:
                continue
            ang = (pin[4] + rot) % 360
            if net in D.POWER_NETS:
                power(net, pt[0], pt[1], POWER_ROT[net][ang])
            else:
                label(net, pt[0], pt[1], LABEL_DIR[ang])
    nc = {}
    for pin in pins:
        pt = pin_end(x, y, pin, rot)
        if pin[0] not in padnets and pt not in done_pts and not pin[5]:
            no_connect(*pt)
            done_pts.add(pt)
            nm = pin[1].replace("/", "{slash}")     # KiCad leaves an empty ("~") pin name out of the net name
            nc[pin[0]] = ("unconnected-(%s-%s-Pad%s)" % (p["ref"], nm, pin[0])) if nm not in ("", "~") else ("unconnected-(%s-Pad%s)" % (p["ref"], pin[0]))
    netlist.append(dict(ref=p["ref"], fp=p["fp"], value=p["value"], uuid=suuid, pads=padnets, nc=nc,
                        in_bom=in_bom, dnp=p.get("dnp", False), silk=p.get("silk", ""), side=p.get("side", "front")))

for net, (x, y) in D.PWR_FLAGS:
    pwr_n[0] += 1
    place_symbol("power:PWR_FLAG", flat_symbol("power", "PWR_FLAG"), "#FLG%02d" % pwr_n[0], "PWR_FLAG", "", x, y,
                 in_bom=False, desc_xy=(x, y - 3.81))
    if net in D.POWER_NETS:
        power(net, x, y, 0 if net == "GND" else 180)
    else:
        label(net, x, y, 270)

for net, pt, r in getattr(D, "POWERS", []):
    x, y = point(pt)
    power(net, x, y, r)
for net, pt, ang in getattr(D, "LABELS", []):
    x, y = point(pt)
    label(net, x, y, ang)
def inside(pt, a, b):
    (x, y), (x0, y0), (x1, y1) = pt, a, b
    if pt in (a, b):
        return False
    if x0 == x1 == x:
        return min(y0, y1) < y < max(y0, y1)
    if y0 == y1 == y:
        return min(x0, x1) < x < max(x0, x1)
    return False
ends = [e for seg in SEGS for e in seg]
JUNC = set()
for pt in set(ends):
    if any(inside(pt, a, b) for a, b in SEGS) or ends.count(pt) >= 3:
        JUNC.add(pt)
for ref, (x, y, rot, pins) in POS.items():         # a pin end landing on the middle of a wire (test point, TVS)
    for pin in pins:
        pt = pin_end(x, y, pin, rot)
        if any(inside(pt, a, b) for a, b in SEGS):
            JUNC.add(pt)
# split every wire at the junctions on it (KiCad's editor does this on load; kicad-cli's ERC does not)
for a, b in SEGS:
    cuts = sorted([j for j in JUNC if inside(j, a, b)], key=lambda j: abs(j[0] - a[0]) + abs(j[1] - a[1]))
    for p0, p1 in zip([a] + cuts, cuts + [b]):
        items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), p0[0], p0[1]], [Sym("xy"), p1[0], p1[1]]],
                      [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]], [Sym("uuid"), uid()]])
for x, y in sorted(JUNC):
    items.append([Sym("junction"), [Sym("at"), x, y], [Sym("diameter"), 0], [Sym("color"), 0, 0, 0, 0], [Sym("uuid"), uid()]])

for t, x, y in D.NOTES:
    text(t, x, y)

def rect(x0, y0, x1, y1):
    items.append([Sym("rectangle"), [Sym("start"), x0, y0], [Sym("end"), x1, y1],
                  [Sym("stroke"), [Sym("width"), 0.3], [Sym("type"), Sym("dash")]], [Sym("fill"), [Sym("type"), Sym("none")]],
                  [Sym("uuid"), uid()]])
for title, x0, y0, x1, y1 in getattr(D, "BOXES", []):    # block frames with a title, so the sheet reads by function
    rect(x0, y0, x1, y1)
    text(title, x0 + 2.54, y0 + 4.32, 2.0)

# ---- write files -------------------------------------------------------------------
lib_block = [Sym("lib_symbols")]
for lib_id, sym in lib_syms.items():
    s = copy.deepcopy(sym)
    s[1] = lib_id
    lib_block.append(s)

sch = [Sym("kicad_sch"), [Sym("version"), 20250114], [Sym("generator"), "eeschema"], [Sym("generator_version"), "9.0"],
       [Sym("uuid"), ROOT], [Sym("paper"), D.PAPER],
       [Sym("title_block"), [Sym("title"), D.TITLE],
        [Sym("date"), D.DATE], [Sym("rev"), D.REV], [Sym("company"), "Agri Sensors and Controls"],
        [Sym("comment"), 1, "Generated by tools/gen_sch.py from tools/design.py - edit design.py and regenerate"],
        [Sym("comment"), 2, D.COMMENT]],
       lib_block] + items + [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]],
                             [Sym("embedded_fonts"), Sym("no")]]
open(os.path.join(PRJ, D.PROJECT + ".kicad_sch"), "w").write(dump(sch) + "\n")
lib = [Sym("kicad_symbol_lib"), [Sym("version"), 20241209], [Sym("generator"), "kicad_symbol_editor"],
       [Sym("generator_version"), "9.0"]] + [make() for make in ASC_SYMBOLS.values()]
open(os.path.join(PRJ, "ASC.kicad_sym"), "w").write(dump(lib) + "\n")
json.dump(netlist, open(os.path.join(HERE, "netlist-%s.json" % D.PROJECT), "w"), indent=1)
print("schematic: %d parts, %d power symbols" % (len(netlist), pwr_n[0]))
