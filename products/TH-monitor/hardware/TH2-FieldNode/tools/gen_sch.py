"""Generate the TH2 field node schematic, project library and netlist.json.

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

def _pin(kind, x, y, ang, name, num):
    return ('(pin %s line (at %s %s %d) (length 2.54) (name "%s" (effects (font (size 1.27 1.27)))) '
            '(number "%s" (effects (font (size 1.27 1.27)))))' % (kind, x, y, ang, name, num))

# CN3058E (Consonance) 1 A linear LiFePO4 charger, eSOP-8 + exposed pad. Pin functions per datasheet DSE-CN3058E:
# 1 TEMP, 2 ISET (ICH = 1218 V / RISET), 3 GND, 4 VIN (3.8-6 V), 5 BAT (3.6 V CV), 6 DONE, 7 CHRG (open drain), 8 CE.
CN3058E = sexpr.parse('''(symbol "CN3058E" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)
 (property "Reference" "U" (at -7.62 11.43 0) (effects (font (size 1.27 1.27)) (justify left)))
 (property "Value" "CN3058E" (at 1.27 11.43 0) (effects (font (size 1.27 1.27)) (justify left)))
 (property "Footprint" "Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.3mm_ThermalVias" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (property "Datasheet" "http://www.consonance-elec.com/pdf/datasheet/DSE-CN3058E.pdf" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (property "Description" "1A linear LiFePO4 charger, 3.6V CV, eSOP-8 with exposed pad (Consonance)" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
 (symbol "CN3058E_0_1"
  (rectangle (start -7.62 10.16) (end 7.62 -7.62) (stroke (width 0.254) (type default)) (fill (type background))))
 (symbol "CN3058E_1_1"
  %s %s %s %s %s %s %s %s %s)
 (embedded_fonts no))''' % (
    _pin("power_in", -10.16, 7.62, 0, "VIN", "4"),
    _pin("input", -10.16, 2.54, 0, "CE", "8"),
    _pin("input", -10.16, -2.54, 0, "TEMP", "1"),
    _pin("passive", -10.16, -5.08, 0, "ISET", "2"),
    _pin("passive", 10.16, 7.62, 180, "BAT", "5"),
    _pin("open_collector", 10.16, 2.54, 180, "~{CHRG}", "7"),
    _pin("open_collector", 10.16, 0, 180, "~{DONE}", "6"),
    _pin("power_in", -1.27, -10.16, 90, "GND", "3"),
    _pin("passive", 5.08, -10.16, 90, "EP", "9")))

AWD_LIB = os.path.join(PRJ, "../../../AWD1-paddy/hardware/AWD-FieldNode/AWD.kicad_sym")

def isc_sx1262():
    """AWD/WPC's proven ISC-SX1262-B symbol (Ra-01 footprint, BUSY on pad 10)."""
    for s in find(sexpr.parse(open(AWD_LIB).read()), "symbol"):
        if s[1] == "ISC-SX1262-B":
            return copy.deepcopy(s)
    raise KeyError("ISC-SX1262-B not found in AWD.kicad_sym")

def get_symbol(p):
    lib, name = p["lib"].split(":")
    if lib == "TH2":
        return isc_sx1262() if name == "ISC-SX1262-B" else copy.deepcopy(CN3058E)
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

# reference/value text offsets for the large symbols (relative to the symbol origin)
DESC = {"U1": (-30.48, -30.48), "D1": (-2.54, 3.81), "D2": (-2.54, 3.81), "D3": (-2.54, 3.81),
        "Y1": (-2.54, 3.81), "SW1": (-2.54, 3.81), "U6": (5.08, -19.05), "J1": (-7.62, -29.21), "U2": (-7.62, -15.24), "U4": (-7.62, -13.97),
        "U3": (-7.62, -10.16), "U5": (-22.86, -6.35)}

netlist = []
for p in D.P:
    sym = get_symbol(p)
    lib_id = p["lib"]
    x, y = p["xy"]
    pins = sym_pins(sym)
    desc = (x + DESC[p["ref"]][0], y + DESC[p["ref"]][1]) if p["ref"] in DESC else None
    in_bom = not p["ref"].startswith("TP")
    suuid = place_symbol(lib_id, sym, p["ref"], p["value"], p["fp"], x, y, desc_xy=desc, in_bom=in_bom)
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
                        in_bom=in_bom))

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

text("USB-C / magnetic pogo input -> F1 polyfuse -> CN3058E LiFePO4 charger. CC1/CC2 5.1k pull-downs so any USB-C charger gives 5 V.", 25.4, 33.02)
text("U4 USBLC6 = ESD on D-/D+/VBUS. J2 is the same signals for an enclosure-wall magnetic connector (fit J1 or J2).", 25.4, 36.83)
text("Charger: ICH = 1218 V / R3 = 0.31 A (0.5C). R4 0R: TEMP low = NTC off.", 116.84, 40.64)
text("RED = charging, GREEN = full (LEDs run off VBUS only).", 116.84, 44.45)
text("Battery: LiFePO4 14500 (2.9-3.6 V). Q1 = reverse protection.", 228.6, 40.64)
text("U3 RT9080-33 (2 uA Iq) keeps 3.6 V charge off the C3.", 228.6, 44.45)
text("VBAT sense: R8/R9 2.2M/2.2M, always on (0.8 uA).", 345.44, 40.64)
text("VBUS_DET (IO5, RTC GPIO) wakes the C3 on charger plug-in.", 345.44, 44.45)
text("ESP32-C3-WROOM-02. Straps: IO2 (LoRa NSS) + IO8 (LoRa BUSY) pulled up, IO9 = BOOT/PAIR. USB-Serial/JTAG on IO18/IO19 = flash + log.", 25.4, 152.4)
text("LoRa option (TH2-L): U6 SX1262", 228.6, 255.27)
text("NSS IO2, SCK IO10, MOSI IO21, MISO IO20", 228.6, 259.08)
text("BUSY IO8, DIO1 IO4 (RTC wake), NRESET = EN", 228.6, 262.89)
text("TH2-W: leave U6, AE1, C13, C14 off", 228.6, 266.7)
text("Y1 32.768 kHz on IO0/IO1 = RTC slow clock (CONFIG_RTC_CLK_SRC_EXT_CRYS). Keep IO0/IO1 traces short, guard with GND.", 25.4, 222.25)
text("SHT40 on the BACK side (with SW1, D2, D3). Milled slot around U5 + C12; nothing hot nearby.", 254.0, 152.4)
text("SHT40-AD1B, I2C 0x44.", 297.18, 190.5)

# ---- write files -------------------------------------------------------------------
lib_block = [Sym("lib_symbols")]
for lib_id, sym in lib_syms.items():
    s = copy.deepcopy(sym)
    s[1] = lib_id
    lib_block.append(s)

sch = [Sym("kicad_sch"), [Sym("version"), 20250114], [Sym("generator"), "eeschema"], [Sym("generator_version"), "9.0"],
       [Sym("uuid"), ROOT], [Sym("paper"), "A3"],
       [Sym("title_block"), [Sym("title"), "TH2 field node v0.2 - one 40x60 PCB in the common AWD/TH box"],
        [Sym("date"), "2026-10-09"], [Sym("rev"), "0.2"], [Sym("company"), "Agri Sensors and Controls"],
        [Sym("comment"), 1, "Generated by tools/gen_sch.py - edit design.py and regenerate, or take over by hand"]],
       lib_block] + items + [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]],
                             [Sym("embedded_fonts"), Sym("no")]]
open(os.path.join(PRJ, D.PROJECT + ".kicad_sch"), "w").write(dump(sch) + "\n")

lib = [Sym("kicad_symbol_lib"), [Sym("version"), 20241209], [Sym("generator"), "kicad_symbol_editor"],
       [Sym("generator_version"), "9.0"], CN3058E, isc_sx1262()]
open(os.path.join(PRJ, "TH2.kicad_sym"), "w").write(dump(lib) + "\n")
open(os.path.join(PRJ, "sym-lib-table"), "w").write(
    '(sym_lib_table\n\t(version 7)\n\t(lib (name "TH2")(type "KiCad")(uri "${KIPRJMOD}/TH2.kicad_sym")(options "")(descr "TH2 field node parts"))\n)\n')
open(os.path.join(PRJ, "fp-lib-table"), "w").write(
    '(fp_lib_table\n\t(version 7)\n\t(lib (name "TH2")(type "KiCad")(uri "${KIPRJMOD}/TH2.pretty")(options "")(descr "TH2 field node footprints"))\n)\n')
json.dump(netlist, open(os.path.join(HERE, "netlist.json"), "w"), indent=1)
print("schematic: %d parts, %d power symbols" % (len(netlist), pwr_n[0]))
