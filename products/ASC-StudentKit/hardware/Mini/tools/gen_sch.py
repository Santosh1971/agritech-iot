"""Generate the ASC Mini schematic and tools/netlist.json from design.py.

Adapted from AWD1's generator (products/AWD1-paddy/hardware/AWD-FieldNode/tools):
every pin connects by a net label or power symbol placed on its endpoint, so the
schematic has no hand-drawn wires. Run with any python3:

    KICAD_SYMBOL_DIR=/path/to/kicad/symbols python3 tools/gen_sch.py

KICAD_SYMBOL_DIR defaults to KiCad 9 on macOS. Then open ASC-Mini.kicad_sch in
KiCad 9 and run ERC.
"""
import copy, json, os, re, uuid
import sexpr
from sexpr import Sym, find, first, dump
import design as D

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

ASC_SYMBOLS = {"HF46F": hf46f}

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
    s.append(prop("Reference", ref, rx, ry, hide=power, justify="left"))
    s.append(prop("Value", value, rx, ry + 2.54, hide=False, justify="left"))
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

def pin_end(x, y, p):
    return round(x + p[2], 2), round(y - p[3], 2)

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

netlist = []
for p in D.P:
    sym = get_symbol(p["lib"])
    x, y = p["xy"]
    pins = sym_pins(sym)
    desc = (x - 12.7, y - 35.56) if p["ref"] == "U1" else None
    in_bom = not p["ref"].startswith("H") and not p.get("dnp")
    extra = {}
    if p.get("silk"):
        extra["Silk"] = p["silk"]
    if p.get("side"):
        extra["Side"] = p["side"]
    suuid = place_symbol(p["lib"], sym, p["ref"], p["value"], p["fp"], x, y, desc_xy=desc, in_bom=in_bom, extra=extra, dnp=p.get("dnp", False))
    padnets, done_pts = {}, set()
    for key, net in p["pins"].items():
        for pin in resolve(pins, key):
            padnets[pin[0]] = D.netname(net)
            pt = pin_end(x, y, pin)
            if pt in done_pts:
                continue
            done_pts.add(pt)
            if net in D.POWER_NETS:
                power(net, pt[0], pt[1], POWER_ROT[net][pin[4]])
            else:
                label(net, pt[0], pt[1], LABEL_DIR[pin[4]])
    nc = {}
    for pin in pins:
        pt = pin_end(x, y, pin)
        if pin[0] not in padnets and pt not in done_pts and not pin[5]:
            no_connect(*pt)
            done_pts.add(pt)
            nc[pin[0]] = "unconnected-(%s-%s-Pad%s)" % (p["ref"], pin[1].replace("/", "{slash}"), pin[0])
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

for t, x, y in D.NOTES:
    text(t, x, y)

# ---- write files -------------------------------------------------------------------
lib_block = [Sym("lib_symbols")]
for lib_id, sym in lib_syms.items():
    s = copy.deepcopy(sym)
    s[1] = lib_id
    lib_block.append(s)

sch = [Sym("kicad_sch"), [Sym("version"), 20250114], [Sym("generator"), "eeschema"], [Sym("generator_version"), "9.0"],
       [Sym("uuid"), ROOT], [Sym("paper"), "A3"],
       [Sym("title_block"), [Sym("title"), "ASC Student Kit - Mini rev A"],
        [Sym("date"), "2026-10-08"], [Sym("rev"), "A"], [Sym("company"), "Agri Sensors and Controls"],
        [Sym("comment"), 1, "Generated by tools/gen_sch.py from tools/design.py - edit design.py and regenerate"],
        [Sym("comment"), 2, "Board 97.4 x 47.0 mm, 3 x M3 on the box's upper bosses (kit architecture 8)"]],
       lib_block] + items + [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]],
                             [Sym("embedded_fonts"), Sym("no")]]
open(os.path.join(PRJ, D.PROJECT + ".kicad_sch"), "w").write(dump(sch) + "\n")
lib = [Sym("kicad_symbol_lib"), [Sym("version"), 20241209], [Sym("generator"), "kicad_symbol_editor"],
       [Sym("generator_version"), "9.0"]] + [make() for make in ASC_SYMBOLS.values()]
open(os.path.join(PRJ, "ASC.kicad_sym"), "w").write(dump(lib) + "\n")
json.dump(netlist, open(os.path.join(HERE, "netlist.json"), "w"), indent=1)
print("schematic: %d parts, %d power symbols" % (len(netlist), pwr_n[0]))
