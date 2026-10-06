"""Generate the AWD field node schematic, project library and netlist.json.

Every pin connects by a net label (or power symbol) placed on its endpoint, so the
schematic carries no hand-drawn wires. Run with system python3, then gen_pcb.py.
"""
import copy, json, os, re, uuid
import sexpr
from sexpr import Sym, find, first, dump
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
KLIB = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/"
WPC_SCH = os.path.join(PRJ, "../../../WPC-pumpcontroller/hardware/WPC/WPC.kicad_sch")

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

def isc_sx1262():
    """WPC's proven ISC-SX1262-B symbol (Ra-01 footprint, BUSY on pad 10), renamed into the AWD library."""
    t = sexpr.parse(open(WPC_SCH).read())
    for s in find(first(t, "lib_symbols"), "symbol"):
        if s[1] == "RF_Module:Ai-Thinker-Ra-01":
            s = copy.deepcopy(s)
            s[1] = "ISC-SX1262-B"
            for sub in find(s, "symbol"):
                sub[1] = sub[1].replace("Ai-Thinker-Ra-01", "ISC-SX1262-B")
            for pr in find(s, "property"):
                if pr[1] == "Footprint":
                    pr[2] = "RF_Module:Ai-Thinker-Ra-01-LoRa"
            return s
    raise KeyError("Ai-Thinker-Ra-01 not found in WPC schematic")

SENSEPAD = sexpr.parse('''(symbol "SensePad" (pin_numbers (hide yes)) (pin_names (offset 0) (hide yes))
 (exclude_from_sim yes) (in_bom no) (on_board yes)
 (property "Reference" "E" (at 3.81 1.27 0) (effects (font (size 1.27 1.27)) (justify left)))
 (property "Value" "SensePad" (at 3.81 -1.27 0) (effects (font (size 1.27 1.27)) (justify left)))
 (property "Footprint" "AWD:SensePad_8x18mm" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (property "Description" "Coated capacitive water-level electrode (copper on F.Cu, no mask opening)" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (symbol "SensePad_0_1"
  (rectangle (start -2.54 2.54) (end 2.54 -2.54) (stroke (width 0.254) (type default)) (fill (type background)))
  (polyline (pts (xy -1.27 -1.27) (xy 1.27 -1.27)) (stroke (width 0) (type default)) (fill (type none)))
  (polyline (pts (xy -1.27 0) (xy 1.27 0)) (stroke (width 0) (type default)) (fill (type none))))
 (symbol "SensePad_1_1"
  (pin passive line (at 0 5.08 270) (length 2.54) (name "E" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27))))))
 (embedded_fonts no))''')

def get_symbol(p):
    lib, name = p["lib"].split(":")
    if lib == "AWD":
        return isc_sx1262() if name == "ISC-SX1262-B" else copy.deepcopy(SENSEPAD)
    return flat_symbol(lib, name)

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

def place_symbol(lib_id, sym, ref, value, fp, x, y, rot=0, in_bom=True, desc_xy=None):
    lib_syms.setdefault(lib_id, sym)
    s = [Sym("symbol"), [Sym("lib_id"), lib_id], [Sym("at"), x, y, rot], [Sym("unit"), 1],
         [Sym("exclude_from_sim"), Sym("no")], [Sym("in_bom"), Sym("yes" if in_bom else "no")],
         [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")], [Sym("uuid"), uid()]]
    power = ref.startswith("#")
    rx, ry = desc_xy or (x + 3.81, y - 1.27)
    s.append(prop("Reference", ref, rx, ry, hide=power, justify="left"))
    s.append(prop("Value", value, rx, ry + 2.54, hide=False, justify="left"))
    s.append(prop("Footprint", fp, x, y, hide=True))
    s.append(prop("Datasheet", "", x, y, hide=True))
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
POWER_ROT = {"GND": {90: 0, 270: 180, 0: 270, 180: 90}, "+3V3": {270: 0, 90: 180, 0: 90, 180: 270}}

def label(net, x, y, ang):
    just = "left bottom" if ang in (0, 90) else "right bottom"
    eff = copy.deepcopy(font)
    eff.append([Sym("justify")] + [Sym(j) for j in just.split()])
    items.append([Sym("label"), net, [Sym("at"), x, y, ang], [Sym("fields_autoplaced"), Sym("yes")], eff, [Sym("uuid"), uid()]])

def power(net, x, y, rot):
    pwr_n[0] += 1
    lib_id = "power:" + net
    place_symbol(lib_id, flat_symbol("power", net), "#PWR%02d" % pwr_n[0], net, "", x, y, rot, in_bom=False,
                 desc_xy=(x, y + (5.08 if net == "GND" and rot == 0 else -5.08)))

def no_connect(x, y):
    items.append([Sym("no_connect"), [Sym("at"), x, y], [Sym("uuid"), uid()]])

netlist = []
for p in D.P:
    sym = get_symbol(p)
    lib_id = p["lib"]
    x, y = p["xy"]
    pins = sym_pins(sym)
    desc = (x - 12.7, y - 52.07) if p["ref"] == "U1" else ((x - 7.62, y - 22.86) if p["ref"] == "U2" else None)
    suuid = place_symbol(lib_id, sym, p["ref"], p["value"], p["fp"], x, y, desc_xy=desc,
                         in_bom=not lib_id.startswith("AWD:Sense") and p["ref"] != "TP1")
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
            # KiCad's own name for a no-connect pin's net, so schematic parity matches
            nc[pin[0]] = "unconnected-(%s-%s-Pad%s)" % (p["ref"], pin[1].replace("/", "{slash}"), pin[0])
    netlist.append(dict(ref=p["ref"], fp=p["fp"], value=p["value"], uuid=suuid, pads=padnets, nc=nc,
                        in_bom=not p["lib"].startswith("AWD:Sense") and p["ref"] != "TP1"))

for net, (x, y) in D.PWR_FLAGS:
    pwr_n[0] += 1
    place_symbol("power:PWR_FLAG", flat_symbol("power", "PWR_FLAG"), "#FLG%02d" % pwr_n[0], "PWR_FLAG", "", x, y,
                 in_bom=False, desc_xy=(x, y - 3.81))
    if net in D.POWER_NETS:
        power(net, x, y, 0 if net == "GND" else 180)   # flag sits above the point; the power symbol hangs below
    else:
        label(net, x, y, 270)

def text(t, x, y, size=1.27):
    items.append([Sym("text"), t, [Sym("exclude_from_sim"), Sym("no")], [Sym("at"), x, y, 0],
                  [Sym("effects"), [Sym("font"), [Sym("size"), size, size]], [Sym("justify"), Sym("left"), Sym("bottom")]],
                  [Sym("uuid"), uid()]])

text("Power: LiFePO4 14500 straight onto the 3V3 rail (3.0-3.4 V plateau, no regulator, ~0 uA quiescent). Q1 = reverse-polarity protection.", 25.4, 33.02)
text("LoRa: ISC-SX1262-B in Ra-01 footprint - same module and pin map as WPC (BUSY = pad 10). SMA -> pigtail -> antenna on a 1.5 m pole above the canopy.", 210.82, 33.02)
text("Battery sense: Q2 enables the 1M/1M divider only while sampling VBAT_ADC (ADC2, WiFi off).", 160.02, 111.76)
text("Sensing strip: E1..E12 = 8x18 mm pads on 20 mm pitch (P1 = +7..+5 cm ... P12 = -15..-17 cm), E13 = dry reference, E14 = GND water-return pad.", 25.4, 203.2)
text("Pads sit on F.Cu with NO mask opening; traces run on In2.Cu between In1 (hatched GND) and B.Cu (solid GND). Coat the whole strip.", 25.4, 207.01)
text("RS1..RS13 = 510R touch series resistors, placed at the strip neck in the head.", 25.4, 228.6)

# ---- write files -------------------------------------------------------------------
lib_block = [Sym("lib_symbols")]
for lib_id, sym in lib_syms.items():
    s = copy.deepcopy(sym)
    s[1] = lib_id
    lib_block.append(s)

sch = [Sym("kicad_sch"), [Sym("version"), 20250114], [Sym("generator"), "eeschema"], [Sym("generator_version"), "9.0"],
       [Sym("uuid"), ROOT], [Sym("paper"), "A3"],
       [Sym("title_block"), [Sym("title"), "AWD field node - segmented capacitive level strip"],
        [Sym("date"), "2026-10-06"], [Sym("rev"), "0.1"], [Sym("company"), "Agri Sensors and Controls"],
        [Sym("comment"), 1, "Generated by tools/gen_sch.py - edit design.py and regenerate, or take over by hand"]],
       lib_block] + items + [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]],
                             [Sym("embedded_fonts"), Sym("no")]]
open(os.path.join(PRJ, D.PROJECT + ".kicad_sch"), "w").write(dump(sch) + "\n")

lib = [Sym("kicad_symbol_lib"), [Sym("version"), 20241209], [Sym("generator"), "kicad_symbol_editor"],
       [Sym("generator_version"), "9.0"], SENSEPAD, isc_sx1262()]
open(os.path.join(PRJ, "AWD.kicad_sym"), "w").write(dump(lib) + "\n")
open(os.path.join(PRJ, "sym-lib-table"), "w").write(
    '(sym_lib_table\n\t(version 7)\n\t(lib (name "AWD")(type "KiCad")(uri "${KIPRJMOD}/AWD.kicad_sym")(options "")(descr "AWD field node parts"))\n)\n')
open(os.path.join(PRJ, "fp-lib-table"), "w").write(
    '(fp_lib_table\n\t(version 7)\n\t(lib (name "AWD")(type "KiCad")(uri "${KIPRJMOD}/AWD.pretty")(options "")(descr "AWD sensing pads"))\n)\n')
json.dump(netlist, open(os.path.join(HERE, "netlist.json"), "w"), indent=1)
print("schematic: %d parts, %d power symbols" % (len(netlist), pwr_n[0]))
