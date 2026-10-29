"""Generate TestJig.kicad_sch from design.py (net-label style schematic)."""
import copy
import math
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402
from sexpr import Q, parse, dump, find, findall  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KLIB = '/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols'
PROJECT = 'TestJig'
G = 2.54
GLOBAL_NETS = {'GND', '+3V3', '+5V'}

_libcache = {}


def U(seed=None):
    if seed is None:
        return str(uuid.uuid4())
    return str(uuid.uuid5(uuid.NAMESPACE_URL, 'testjig/' + seed))


def load_lib(lib):
    if lib not in _libcache:
        path = os.path.join(ROOT, 'TestJig.kicad_sym') if lib == 'TestJig' else os.path.join(KLIB, lib + '.kicad_sym')
        tree = parse(open(path).read())[0]
        _libcache[lib] = {s[1]: s for s in findall(tree, 'symbol')}
    return _libcache[lib]


def flat_symbol(lib_id):
    lib, name = lib_id.split(':')
    syms = load_lib(lib)
    s = copy.deepcopy(syms[name])
    ext = find(s, 'extends')
    if ext:
        parent = flat_symbol(lib + ':' + ext[1])
        pname = parent[1].split(':')[1]
        s = [x for x in s if not (isinstance(x, list) and x[0] == 'extends')]
        for sub in findall(parent, 'symbol'):
            sub = copy.deepcopy(sub)
            sub[1] = Q(name + sub[1][len(pname):])
            s.append(sub)
        have = {str(x[1]) for x in findall(s, 'property')}
        for prop in findall(parent, 'property'):
            if str(prop[1]) not in have:
                idx = max(i for i, x in enumerate(s) if isinstance(x, list) and x[0] == 'property')
                s.insert(idx + 1, copy.deepcopy(prop))
        # inherit flags the child lacks
        for key in ('pin_names', 'pin_numbers', 'power'):
            if not find(s, key) and find(parent, key):
                s.insert(2, copy.deepcopy(find(parent, key)))
    s[1] = Q(lib_id)
    return s


def sym_pins(fs):
    """Return list of dict(number, name, x, y, ang, unit, hidden, etype)."""
    out = []
    name = fs[1].split(':')[1]
    for sub in findall(fs, 'symbol'):
        suf = sub[1][len(name) + 1:].split('_')
        unit, style = int(suf[0]), int(suf[1])
        if style not in (0, 1):
            continue
        for p in findall(sub, 'pin'):
            at = find(p, 'at')
            hidden = 'hide' in p or (find(p, 'hide') is not None and find(p, 'hide')[1] == 'yes')
            out.append(dict(number=str(find(p, 'number')[1]), name=str(find(p, 'name')[1]),
                            x=float(at[1]), y=float(at[2]), ang=int(float(at[3])) if len(at) > 3 else 0,
                            unit=unit, hidden=hidden, etype=p[1]))
    return out


def xform(px, py, rot):
    """lib (y-up) point -> schematic offset (y-down) for a symbol rotated `rot` deg CCW."""
    x, y = px, -py
    t = math.radians(rot)
    c, s = round(math.cos(t)), round(math.sin(t))
    return x * c + y * s, -x * s + y * c


def label_angle(pin_ang, rot):
    a = (pin_ang + rot) % 360
    return {0: 180, 180: 0, 90: 270, 270: 90}[a]


def snap(v):
    return round(v / G) * G


def text_len(t, size=1.27):
    return len(t) * size * 0.85 + 1.5


# --------------------------------------------------------------- build instances
lib_syms = {}
instances = []  # (part, unit, pins_for_unit)
for p in design.PARTS:
    if p['lib'] not in lib_syms:
        lib_syms[p['lib']] = flat_symbol(p['lib'])
    pins = sym_pins(lib_syms[p['lib']])
    for u in p['units']:
        up = [q for q in pins if q['unit'] in (0, u)]
        instances.append((p, u, up))


def bbox(p, u, pins):
    rot = p['rot']
    xs, ys = [], []
    for q in pins:
        if q['hidden']:
            continue
        dx, dy = xform(q['x'], q['y'], rot)
        xs.append(dx)
        ys.append(dy)
        net = p['nets'].get(q['number'])
        L = text_len(net) if net else 1.5
        la = label_angle(q['ang'], rot)
        if la == 0:
            xs.append(dx + L)
        elif la == 180:
            xs.append(dx - L)
        elif la == 90:
            ys.append(dy - L)
        else:
            ys.append(dy + L)
    if not xs:
        xs, ys = [-2, 2], [-2, 2]
    return min(xs) - 1.5, min(ys) - 4.5, max(xs) + 1.5, max(ys) + 4.5



# --------------------------------------------------------------- hand-drawn blocks (wired, readable circuits)
# Relative coordinates (mm, 1.27 grid) from the block's drawing origin. (ref, unit) -> (x, y, rot, mirror_x)
def ao_manual(n, U_, Qn, Qp, D, rb, cb, JP, Cd):
    return {
        ('R%d' % rb, 1): (10.16, 25.4, 90, False), ('C%d' % cb, 1): (13.97, 29.21, 0, False),
        ('R%d' % (rb + 1), 1): (21.59, 25.4, 90, False), ('C%d' % (cb + 1), 1): (25.4, 29.21, 0, False),
        (U_, 1): (40.64, 27.94, 0, False), ('R%d' % (rb + 2), 1): (52.07, 27.94, 90, False),
        (Qn, 1): (60.96, 27.94, 0, False), ('R%d' % (rb + 4), 1): (63.5, 12.7, 0, False),
        ('R%d' % (rb + 3), 1): (63.5, 40.64, 0, False),
        (U_, 2): (81.28, 21.59, 0, False), ('R%d' % (rb + 5), 1): (92.71, 21.59, 90, False),
        (Qp, 1): (101.6, 21.59, 0, 'x'), ('R%d' % (rb + 6), 1): (104.14, 6.35, 0, False),
        (D, 1): (104.14, 31.75, 90, False),
        (U_, 3): (121.92, 25.4, 0, False), (Cd, 1): (134.62, 25.4, 0, False),
        (JP, 1): (157.48, 25.4, 0, False), ('R%d' % (rb + 7), 1): (167.64, 27.94, 0, False),
    }


AO_WIRES = [  # polylines, relative mm
    [(13.97, 25.4), (17.78, 25.4)],                                   # F1 -> R(b+1)
    [(25.4, 25.4), (33.02, 25.4)],                                    # VSET -> U A +
    [(63.5, 16.51), (63.5, 19.05)], [(63.5, 19.05), (63.5, 22.86)],   # R(b+4) -> node A -> Qn C
    [(63.5, 19.05), (73.66, 19.05)],                                  # node A -> U B +
    [(63.5, 33.02), (63.5, 35.56)], [(63.5, 35.56), (63.5, 36.83)],   # Qn E -> node QE -> R(b+3)
    [(63.5, 35.56), (30.48, 35.56), (30.48, 30.48), (33.02, 30.48)],  # QE feedback -> U A -
    [(104.14, 10.16), (104.14, 12.7)], [(104.14, 12.7), (104.14, 16.51)],  # R(b+6) -> node B -> Qp E
    [(104.14, 12.7), (111.76, 12.7), (111.76, 45.72), (71.12, 45.72), (71.12, 24.13), (73.66, 24.13)],  # B -> U B -
    [(104.14, 26.67), (104.14, 27.94)],                               # Qp C -> D A
]
AO_JUNCTIONS = [(13.97, 25.4), (25.4, 25.4), (63.5, 19.05), (63.5, 35.56), (104.14, 12.7)]
AO_LABELS = [  # (suffix, x, y, angle)  one visible name per wired net
    ('F1', 13.97, 25.4, 90), ('VSET', 25.4, 25.4, 90), ('U1', 48.26, 27.94, 90), ('QB1', 55.88, 27.94, 90),
    ('A', 66.04, 19.05, 0), ('QE', 60.96, 35.56, 180), ('U2', 88.9, 21.59, 90), ('QB2', 96.52, 21.59, 90),
    ('B', 106.68, 12.7, 0), ('QC', 104.14, 27.94, 180),
]
AO_NOTES = [
    (8.0, 52.0, 'Stage 1 (op-amp A + NPN): sinks I1 = Vset / R(10k) through R(4k7)  ->  V(4k7) = Vset x 0.47'),
    (8.0, 56.5, 'Stage 2 (op-amp B + PNP): forces the same drop across R(47R)  ->  Iout = Vset x 4k7 / (10k x 47R) = Vset x 10 mA/V'),
    (8.0, 61.0, 'PWM 12-bit, 2-pole RC (10k/1uF x2) -> Vset.  D1 blocks reverse current.  JP1 + 150R = local self-test load (ADC readback).'),
]
def ao_layout(n, *args):
    parts = ao_manual(n, *args)
    return dict(parts=parts, wires=[list(w) for w in AO_WIRES], junctions=list(AO_JUNCTIONS),
                labels=[('AO%d_%s' % (n, suf), x, y, a) for (suf, x, y, a) in AO_LABELS],
                notes=list(AO_NOTES), height=66)


LAYOUTS = {
    'ao1': ao_layout(1, 'U8', 'Q1', 'Q2', 'D1', 20, 12, 'JP1', 'C10'),
    'ao2': ao_layout(2, 'U9', 'Q3', 'Q4', 'D2', 30, 14, 'JP2', 'C11'),
}
LAYOUTS['ao2']['parts'].update({('C20', 1): (144.78, 25.4, 0, False), ('J10', 1): (190.5, 25.4, 0, False)})

_PART_LIB = {p['ref']: p['lib'] for p in design.PARTS}


def pin_rel(parts, ref, num, unit=1):
    """Connection point of pin `num` of (ref, unit) placed per `parts`, in the layout's frame."""
    x, y, rot, mir = parts[(ref, unit)][:4]
    for q in sym_pins(lib_syms[_PART_LIB[ref]]):
        if q['number'] == str(num) and q['unit'] in (0, unit):
            dx, dy = xform(q['x'], q['y'], rot)
            if mir == 'x':
                dy = -dy
            elif mir == 'y':
                dx = -dx
            return (round(x + dx, 2), round(y + dy, 2))
    raise KeyError((ref, num))


import sch_layout  # noqa: E402
LAYOUTS.update(sch_layout.build(pin_rel))
MANUAL = {bid: L['parts'] for bid, L in LAYOUTS.items()}

# --------------------------------------------------------------- layout blocks (A1 sheet, 841 x 594)
BLOCK_RECTS = {
    'mcu': (15, 20, 190),
    'lora': (15, 95, 190),
    'pulse': (15, 165, 190),
    'adc': (15, 280, 190),
    'mech': (15, 345, 190),
    'sense': (220, 20, 300),
    'jig': (220, 172, 300),
    'ao1': (220, 320, 300),
    'out': (535, 20, 290),
    'led': (535, 162, 290),
    'ao2': (535, 320, 290),
}

placed = []  # (part, unit, pins, X, Y)
block_extent = {}
for bid, title in design.BLOCKS:
    x0, y0, w = BLOCK_RECTS[bid]
    cx, cy = x0 + 3, y0 + 12
    row_h = 0
    if bid in MANUAL:
        ox, oy = snap(x0 + 3), snap(y0 + 12)
        for (p, u, pins) in instances:
            if p['block'] == bid:
                mx, my, mrot, mmir = MANUAL[bid][(p['ref'], u)][:4]
                placed.append((p, u, pins, round(ox + mx, 2), round(oy + my, 2), mrot, mmir))
        block_extent[bid] = (x0, y0, x0 + w, oy + LAYOUTS[bid]['height'])
        continue
    for (p, u, pins) in instances:
        if p['block'] != bid:
            continue
        bx0, by0, bx1, by1 = bbox(p, u, pins)
        bw, bh = bx1 - bx0, by1 - by0
        if cx + bw > x0 + w and cx > x0 + 3:
            cx = x0 + 3
            cy += row_h + 4
            row_h = 0
        X = snap(cx - bx0 + 1.27)
        Y = snap(cy - by0 + 1.27)
        placed.append((p, u, pins, X, Y, p['rot'], False))
        cx += bw + 4
        row_h = max(row_h, bh)
    block_extent[bid] = (x0, y0, x0 + w, cy + row_h + 4)

# --------------------------------------------------------------- emit
root_uuid = U('root')
items = []


def eff(size=1.27, justify=None, hide=False, bold=False):
    font = [ 'font', ['size', size, size]]
    if bold:
        font.append(['bold', 'yes'])
    e = ['effects', font]
    if justify:
        e.append(['justify'] + justify)
    if hide:
        e.append(['hide', 'yes'])
    return e


for bid, title in design.BLOCKS:
    x0, y0, x1, y1 = block_extent[bid]
    items.append(['rectangle', ['start', x0, y0], ['end', x1, y1],
                  ['stroke', ['width', 0.3], ['type', 'dash']], ['fill', ['type', 'none']], ['uuid', Q(U('rect' + bid))]])
    items.append(['text', Q(title), ['exclude_from_sim', 'no'], ['at', x0 + 2, y0 + 6, 0],
                  eff(2.5, ['left', 'bottom'], bold=True), ['uuid', Q(U('title' + bid))]])

# pins in hand-drawn blocks that a wire reaches, or that touch another pin, need no net label
DRAWN = set()
_pin_count = {}
for bid, L in LAYOUTS.items():
    x0, y0, _x1, _y1 = block_extent[bid]
    ox, oy = snap(x0 + 3), snap(y0 + 12)
    for line in L['wires']:
        for (wx_, wy_) in line:
            DRAWN.add((round(ox + wx_, 2), round(oy + wy_, 2)))
    for (ref, u), _pl in L['parts'].items():
        for q in sym_pins(lib_syms[_PART_LIB[ref]]):
            if q['unit'] in (0, u) and not q['hidden']:
                pr = pin_rel(L['parts'], ref, q['number'], u)
                key = (round(ox + pr[0], 2), round(oy + pr[1], 2))
                _pin_count[key] = _pin_count.get(key, 0) + 1
DRAWN |= {k for k, c in _pin_count.items() if c > 1}

label_n = 0
for (p, u, pins, X, Y, rot, mir) in placed:
    ref = p['ref']
    bx0, by0, bx1, by1 = bbox(dict(p, rot=rot), u, pins)
    is_power = ref.startswith('#')
    two_pin = len(pins) <= 3 and rot in (90, 270)
    if two_pin:
        ref_at = [X, Y - 2.54, 0]
        val_at = [X, Y + 2.54, 0]
    else:
        ref_at = [X, Y + by0 + 2.5, 0]
        val_at = [X, Y + by1 - 2.0, 0]
    fjust = None
    if p['block'] in MANUAL:
        pp = []
        for q in pins:
            dx, dy = xform(q['x'], q['y'], rot)
            pp.append((round(-dx if mir == 'y' else dx, 2), round(-dy if mir == 'x' else dy, 2)))
        if len(pp) == 2 and pp[0][0] == pp[1][0] and rot in (90, 270):   # rotated part: fields turn too
            ref_at, val_at = [X + 2.2, Y, 0], [X + 4.6, Y, 0]
        elif len(pp) == 2 and pp[0][0] == pp[1][0]:      # vertical 2-pin part: fields to the right
            ref_at, val_at, fjust = [X + 1.9, Y - 1.0, 0], [X + 1.9, Y + 1.9, 0], ['left']
        elif len(pp) == 2:                                # horizontal 2-pin part: above / below
            ref_at, val_at = [X, Y - 2.2, 0], [X, Y + 3.0, 0]
        elif p['lib'].startswith('Transistor_BJT'):
            ref_at, val_at, fjust = [X + 5.1, Y - 1.3, 0], [X + 5.1, Y + 1.9, 0], ['left']
        elif 'MCP6002' in p['lib'] and u in (1, 2):       # op-amp halves: ref above, value on the power unit only
            ref_at, val_at, fjust = [X - 1.27, Y - 6.35, 0], [X, Y, 0], ['left']
        else:                                             # ICs / connectors: ref at the top, value at the bottom
            ys = [q[1] for q in pp]
            ref_at, val_at, fjust = [X + 2.54, Y + min(ys) + 1.0, 0], [X + 2.54, Y + max(ys) - 0.4, 0], ['left']
    _ov = MANUAL.get(p['block'], {}).get((ref, u))
    if _ov is not None and len(_ov) > 4:              # layout gives explicit field positions
        f = _ov[4]
        ref_at = [X + f['ref'][0], Y + f['ref'][1], f['ref'][2]]
        val_at = [X + f['val'][0], Y + f['val'][1], f['val'][2]]
        fjust = None
    sym = ['symbol', ['lib_id', Q(p['lib'])], ['at', X, Y, rot]] + ([['mirror', mir]] if mir else []) + [['unit', u],
           ['exclude_from_sim', 'no'], ['in_bom', 'no' if is_power or ref.startswith('H') else 'yes'],
           ['on_board', 'no' if is_power else 'yes'], ['dnp', 'no'], ['uuid', Q(U('sym' + ref + str(u)))],
           ['property', Q('Reference'), Q(ref), ['at'] + ref_at, eff(1.27, fjust, hide=is_power)],
           ['property', Q('Value'), Q(p['value']), ['at'] + val_at,
            eff(1.27, fjust, hide=p['block'] in MANUAL and 'MCP6002' in p['lib'] and u in (1, 2))],
           ['property', Q('Footprint'), Q(p['fp']), ['at', X, Y, 0], eff(1.27, hide=True)],
           ['property', Q('Datasheet'), Q(''), ['at', X, Y, 0], eff(1.27, hide=True)]]
    for q in pins:
        sym.append(['pin', Q(q['number']), ['uuid', Q(U('pin' + ref + str(u) + q['number']))]])
    sym.append(['instances', ['project', Q(PROJECT), ['path', Q('/' + root_uuid), ['reference', Q(ref)], ['unit', u]]]])
    items.append(sym)

    seen = set()
    for q in pins:
        dx, dy = xform(q['x'], q['y'], rot)
        if mir == 'x':
            dy = -dy
        elif mir == 'y':
            dx = -dx
        px, py = round(X + dx, 2), round(Y + dy, 2)
        if (px, py) in seen:
            continue  # stacked pins share the label
        seen.add((px, py))
        net = p['nets'].get(q['number'])
        if q['hidden'] and not net:
            continue
        if net and (px, py) in DRAWN:
            continue                   # pin is reached by a wire or sits on another pin: no label needed
        if net:
            la = label_angle(q['ang'], rot)
            if mir == 'x' and la in (90, 270):
                la = 360 - la
            elif mir == 'y' and la in (0, 180):
                la = 180 - la if la else 180
            just = {0: ['left', 'bottom'], 180: ['right', 'bottom'], 90: ['left', 'bottom'], 270: ['right', 'bottom']}[la]
            label_n += 1
            if net in GLOBAL_NETS:
                items.append(['global_label', Q(net), ['shape', 'passive'], ['at', px, py, la],
                              ['fields_autoplaced', 'yes'], eff(1.27, just), ['uuid', Q(U('lbl%d' % label_n))],
                              ['property', Q('Intersheetrefs'), Q('${INTERSHEET_REFS}'), ['at', px, py, 0],
                               eff(1.27, hide=True)]])
            else:
                items.append(['label', Q(net), ['at', px, py, la], ['fields_autoplaced', 'yes'],
                              eff(1.27, just), ['uuid', Q(U('lbl%d' % label_n))]])
        else:
            items.append(['no_connect', ['at', px, py], ['uuid', Q(U('nc' + ref + str(u) + q['number']))]])

for bid, L in LAYOUTS.items():
    x0, y0, _x1, _y1 = block_extent[bid]
    ox, oy = snap(x0 + 3), snap(y0 + 12)
    for k, line in enumerate(L['wires']):
        for a, b in zip(line, line[1:]):
            items.append(['wire', ['pts', ['xy', round(ox + a[0], 2), round(oy + a[1], 2)],
                                   ['xy', round(ox + b[0], 2), round(oy + b[1], 2)]],
                          ['stroke', ['width', 0], ['type', 'default']], ['uuid', Q(U('w%s%d%s%s' % (bid, k, a, b)))]])
    for (jx, jy) in L['junctions']:
        items.append(['junction', ['at', round(ox + jx, 2), round(oy + jy, 2)], ['diameter', 0],
                      ['color', 0, 0, 0, 0], ['uuid', Q(U('j%s%s' % (bid, (jx, jy))))]])
    for k, (net, lx, ly, la) in enumerate(L['labels']):
        just = ['right', 'bottom'] if la == 180 else ['left', 'bottom']
        items.append(['label', Q(net), ['at', round(ox + lx, 2), round(oy + ly, 2), la],
                      ['fields_autoplaced', 'yes'], eff(1.0, just), ['uuid', Q(U('ml%s%d%s' % (bid, k, net)))]])
    for k, (tx, ty, t) in enumerate(L['notes']):
        items.append(['text', Q(t), ['exclude_from_sim', 'no'], ['at', ox + tx, oy + ty, 0],
                      eff(1.6, ['left', 'bottom']), ['uuid', Q(U('note%s%d' % (bid, k)))]])

sch = ['kicad_sch', ['version', '20250114'], ['generator', Q('eeschema')], ['generator_version', Q('9.0')],
       ['uuid', Q(root_uuid)], ['paper', Q('A1')],
       ['title_block', ['title', Q('TestJig-ESP32 - DUT test jig (WM1 product range)')],
        ['date', Q('2026-10-16')], ['rev', Q('A')], ['company', Q('Draft for review - generated by Claude')],
        ['comment', 1, Q('USB powered from ESP32 DevKit. 16 relay sense (COM+NO), 16 DUT outputs, 2+2 pulse,')],
        ['comment', 2, Q('2x 4-20mA (5V), 4x ESP32 ADC1, SX1262 LoRa sniffer, 4-way DIP, 8 LEDs. 100x100 mm 2-layer.')]],
       ['lib_symbols'] + list(lib_syms.values())] + items + [
       ['sheet_instances', ['path', Q('/'), ['page', Q('1')]]], ['embedded_fonts', 'no']]

open(os.path.join(ROOT, PROJECT + '.kicad_sch'), 'w').write(dump(sch) + '\n')
print('schematic: %d symbol units, %d labels' % (len(placed), label_n))
for bid, e in block_extent.items():
    print(bid, [round(v) for v in e])
