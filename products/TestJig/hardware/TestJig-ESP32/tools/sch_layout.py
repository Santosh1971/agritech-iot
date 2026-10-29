"""Hand-drawn (wired) schematic layouts for the TestJig blocks.

Each builder returns a layout dict:
    parts:     {(ref, unit): (x, y, rot, mirror)}   mirror: False | 'x' (flip vertical) | 'y' (flip horizontal)
    wires:     [polyline, ...]                       polyline = [(x, y), ...]
    junctions: [(x, y), ...]
    labels:    [(net, x, y, angle), ...]             one visible name per wired net (sits on a wire)
    notes:     [(x, y, text), ...]
    wired:     set of nets drawn with wires (their pin labels are suppressed)
    height:    block height in mm
Coordinates are relative to the block's drawing origin, on the 1.27 mm grid.
`pin(parts, ref, num, unit=1)` returns a pin's connection point in the same frame.
"""

G = 2.54
PITCH = 12.7                                # row pitch for the 8-channel banks (room for ref + value)
F_SERIES = {'ref': (0, -3.8, 90), 'val': (0, -2.4, 90)}   # horizontal R on the signal line: text above
F_PULL = {'ref': (0, 2.3, 90), 'val': (0, 3.7, 90)}       # horizontal pull-up under the line: text below


def _new():
    return dict(parts={}, wires=[], junctions=[], labels=[], notes=[], wired=set(), height=60)


def hvh(a, b, xj):
    """a -> horizontal to xj -> vertical -> horizontal to b."""
    pts = [a, (xj, a[1]), (xj, b[1]), b]
    out = [pts[0]]
    for p in pts[1:]:
        if p != out[-1]:
            out.append(p)
    return out


# --------------------------------------------------------------------- input banks (DUT relay sense)
def sense(pin):
    """J1..J4 -> 1k series -> node (10k pull-up to 3V3) -> 74HC165 D0..D7. Two identical groups (U5, U6)."""
    L = _new()
    for g, (ic, cap, conns, base) in enumerate([('U5', 'C6', ('J1', 'J2'), 0), ('U6', 'C7', ('J3', 'J4'), 8)]):
        gx, gy = 0.0 + 147.32 * g, 7.62
        yr = [gy + 7.62 + PITCH * i for i in range(8)]
        P = L['parts']
        for c, conn in enumerate(conns):                     # pin 2 of each connector on its first row
            P[(conn, 1)] = (gx + 5.08, yr[4 * c] + 2.54, 0, 'y')
        for i in range(8):
            ch = base + i + 1
            P[('R%d' % (100 + ch), 1)] = (gx + 27.94, yr[i], 90, False, F_SERIES)          # 1k
            P[('R%d' % (120 + ch), 1)] = (gx + 35.56, yr[i] + 2.54, 90, False, F_PULL)     # 10k pull-up
        x_n = gx + 31.75                                     # node = 1k pin 2 = pull-up pin 1
        xb0 = x_n + 33.02
        P[(ic, 1)] = (xb0 + 2.54 + 12.7, yr[7] - 5.08, 0, False)   # D7 on the last row
        P[(cap, 1)] = (xb0 + 45.72, yr[0] + 2.54, 0, False)
        for i in range(8):
            ch = base + i + 1
            conn = conns[i // 4]
            k = i % 4
            jp = pin(P, conn, str(k + 2))
            r1 = pin(P, 'R%d' % (100 + ch), '1')
            # connector fan-out: row k drops at a column further right for earlier rows
            L['wires'].append(hvh(jp, r1, jp[0] + 2.54 * (4 - k)) if jp[1] != r1[1] else [jp, r1])
            node = pin(P, 'R%d' % (100 + ch), '2')
            pu = pin(P, 'R%d' % (120 + ch), '1')
            L['wires'].append([node, pu])
            d = pin(P, ic, {0: '11', 1: '12', 2: '13', 3: '14', 4: '3', 5: '4', 6: '5', 7: '6'}[i])
            L['wires'].append(hvh(node, d, xb0 - G * i))
            L['junctions'].append(node)
            L['labels'].append(('RLY_NO%d' % ch, r1[0] - 6.35, r1[1], 0))
            L['labels'].append(('RS%d' % ch, node[0] + 1.27, node[1], 0))
            L['wired'] |= {'RLY_NO%d' % ch, 'RS%d' % ch}
    L['notes'] = [(0, 128.0, 'Each channel: DUT relay NO contact -> 1k -> node with 10k pull-up -> 74HC165 input. '
                             'Contact closed (relay ON) = 0, open = 1.  Pin 1 "G" of every connector = COM (GND).')]
    L['height'] = 134
    return L


# --------------------------------------------------------------------- output banks (drive DUT inputs)
def outputs(pin):
    """74HC595 QA..QH -> 330R -> J5..J8. Two identical groups (U2, U3)."""
    L = _new()
    q = ['15', '1', '2', '3', '4', '5', '6', '7']
    for g, (ic, cap, conns, base) in enumerate([('U2', 'C3', ('J5', 'J6'), 0), ('U3', 'C4', ('J7', 'J8'), 8)]):
        gx, gy = 0.0 + 147.32 * g, 7.62
        yr = [gy + 7.62 + PITCH * i for i in range(8)]
        P = L['parts']
        P[(ic, 1)] = (gx + 15.24, yr[7] - 7.62, 0, False)    # QH on the last row
        P[(cap, 1)] = (gx + 5.08, yr[7] + 12.7, 0, False)
        xq = gx + 15.24 + 10.16                              # output pin column
        for i in range(8):
            ch = base + i + 1
            P[('R%d' % (140 + ch), 1)] = (xq + 30.48, yr[i], 90, False, F_SERIES)
        for c, conn in enumerate(conns):
            P[(conn, 1)] = (xq + 63.5, yr[4 * c] + 2.54, 0, False)
        for i in range(8):
            ch = base + i + 1
            qp = pin(P, ic, q[i])
            r1 = pin(P, 'R%d' % (140 + ch), '1')
            L['wires'].append(hvh(qp, r1, xq + 5.08 + G * i))
            r2 = pin(P, 'R%d' % (140 + ch), '2')
            conn = conns[i // 4]
            k = i % 4
            jp = pin(P, conn, str(k + 2))
            L['wires'].append(hvh(r2, jp, jp[0] - 2.54 * (4 - k)) if jp[1] != r2[1] else [r2, jp])
            L['labels'].append(('DO%d' % ch, r1[0] - 6.35, r1[1], 0))
            L['labels'].append(('DUT_OUT%d' % ch, r2[0] + 1.27, r2[1], 0))
            L['wired'] |= {'DO%d' % ch, 'DUT_OUT%d' % ch}
    L['notes'] = [(0, 118.0, 'Each channel: 74HC595 output (3.3 V) -> 330R -> DUT input.  /OE pulled up: all outputs '
                             'Hi-Z until firmware clears the registers and drives /OE low.  Pin 1 "G" = GND.')]
    L['height'] = 124
    return L


# --------------------------------------------------------------------- jig LEDs
def leds(pin):
    L = _new()
    P = L['parts']
    q = ['15', '1', '2', '3', '4', '5', '6', '7']
    yr = [10.16 + PITCH * i for i in range(8)]
    P[('U4', 1)] = (15.24, yr[7] - 7.62, 0, False)
    P[('C5', 1)] = (5.08, yr[7] + 12.7, 0, False)
    xq = 25.4
    for i in range(8):
        P[('R%d' % (161 + i), 1)] = (xq + 30.48, yr[i], 90, False, F_SERIES)
        P[('D%d' % (7 + i), 1)] = (xq + 43.18, yr[i], 180, False, {'ref': (0, -3.6, 0), 'val': (0, 2.6, 0)})
    for i in range(8):
        qp = pin(P, 'U4', q[i])
        r1 = pin(P, 'R%d' % (161 + i), '1')
        L['wires'].append(hvh(qp, r1, xq + 5.08 + G * i))
        r2 = pin(P, 'R%d' % (161 + i), '2')
        a = pin(P, 'D%d' % (7 + i), '2')
        L['wires'].append([r2, a])
        L['labels'].append(('LEDDRV%d' % (i + 1), r1[0] - 6.35, r1[1], 0))
        L['labels'].append(('LEDA%d' % (i + 1), r2[0] + 0.0, r2[1], 0))
        L['wired'] |= {'LEDDRV%d' % (i + 1), 'LEDA%d' % (i + 1)}
    L['notes'] = [(0, 113.0, 'Third 74HC595 (end of the chain): QA..QH -> 470R -> LED -> GND.  PASS, FAIL, BUSY, LORA, USER1-4.')]
    L['height'] = 119
    return L


# --------------------------------------------------------------------- jig inputs (DIP, START, LID, spares)
def jig(pin):
    L = _new()
    P = L['parts']
    yr = [10.16 + PITCH * i for i in range(8)]
    P[('SW1', 1)] = (10.16, yr[0] + 5.08, 0, 'y', {'ref': (0, -9.0, 0), 'val': (0, 6.8, 0)})   # pins 1..4 right
    P[('J12', 1)] = (7.62, yr[4] + 5.08, 0, 'y')          # pins 1..4 = START, LID, SPARE1, SPARE2
    nets = ['DIP1', 'DIP2', 'DIP3', 'DIP4', 'START', 'LID', 'SPARE_IN1', 'SPARE_IN2']
    srcs = [('SW1', '1'), ('SW1', '2'), ('SW1', '3'), ('SW1', '4'), ('J12', '1'), ('J12', '2'), ('J12', '3'), ('J12', '4')]
    x_n = 35.56
    for i in range(8):
        P[('R%d' % (171 + i), 1)] = (x_n + 3.81, yr[i] + 2.54, 90, False, F_PULL)
    xb0 = x_n + 33.02
    P[('U7', 1)] = (xb0 + 2.54 + 12.7, yr[7] - 5.08, 0, False)
    P[('C8', 1)] = (xb0 + 45.72, yr[0] + 2.54, 0, False)
    dpin = ['11', '12', '13', '14', '3', '4', '5', '6']
    for i in range(8):
        sp = pin(P, *srcs[i])
        k = i % 4
        node = (x_n, yr[i])
        L['wires'].append(hvh(sp, node, sp[0] + 2.54 * (4 - k)) if sp[1] != node[1] else [sp, node])
        pu = pin(P, 'R%d' % (171 + i), '1')
        L['wires'].append([node, pu])
        d = pin(P, 'U7', dpin[i])
        L['wires'].append(hvh(node, d, xb0 - G * i))
        L['junctions'].append(node)
        L['labels'].append((nets[i], node[0] + 1.27, node[1], 0))
        L['wired'].add(nets[i])
    L['notes'] = [(0, 123.0, 'DIP ON / button pressed = 0 (switch to GND, 10k pull-up).  J12: START, LID, SPARE1, SPARE2, GND, 3V3.')]
    L['height'] = 129
    return L


# --------------------------------------------------------------------- pulse I/O
def pulse(pin):
    L = _new()
    P = L['parts']
    yr = [10.16 + PITCH * i for i in range(4)]
    P[('J14', 1)] = (7.62, yr[0] + 2.54, 0, 'y')     # PULSE OUT: pins 1, 2 -> rows 0, 1
    P[('J9', 1)] = (7.62, yr[2] + 2.54, 0, 'y')      # PULSE IN:  pins 1, 2 -> rows 2, 3
    rows = [('R4', None, 'PULSE_OUT1', 'PO1'), ('R5', None, 'PULSE_OUT2', 'PO2'),
            ('R6', 'R8', 'PULSE_IN1', 'PI1'), ('R7', 'R9', 'PULSE_IN2', 'PI2')]
    x_n = 33.02
    for i, (rs, rp, net, mcu) in enumerate(rows):
        # R4/R5 have the connector side on pin 2, R6/R7 on pin 1: turn so that side faces the node
        P[(rs, 1)] = (x_n + 3.81, yr[i], 270 if rs in ('R4', 'R5') else 90, False, F_SERIES)
        if rp:
            P[(rp, 1)] = (x_n + 3.81, yr[i] + 2.54, 90, False, F_PULL)
    P[('U11', 1)] = (x_n + 0.0, yr[3] + 22.86, 0, False)
    for i, (rs, rp, net, mcu) in enumerate(rows):
        jp = pin(P, 'J14' if i < 2 else 'J9', str(i % 2 + 1))
        node = (x_n, yr[i])
        k = i % 2
        L['wires'].append(hvh(jp, node, jp[0] + 2.54 * (4 - k)) if jp[1] != node[1] else [jp, node])
        L['junctions'].append(node)
        if rp:
            L['wires'].append([node, (x_n, yr[i] + 2.54)])
        L['labels'].append((net, node[0] - 6.35, node[1], 0))
        L['wired'].add(net)
    L['notes'] = [(0, 92.0, 'PO1/PO2: LEDC/RMT pulse out via 330R.  PI1/PI2: PCNT in via 1k, 10k pull-up (open-collector OK).  '
                            'U11 SRV05-4 clamps all four lines.')]
    L['height'] = 98
    return L


# --------------------------------------------------------------------- ESP32 ADC front ends
def adc(pin):
    L = _new()
    P = L['parts']
    divs = [('AO1', 'R10', 'R11', 'C16', 'ADC_AO1'), ('AO2', 'R12', 'R13', 'C17', 'ADC_AO2'),
            ('AI1', 'R14', 'R15', 'C18', 'ADC_AI1'), ('AI2', 'R16', 'R17', 'C19', 'ADC_AI2')]
    for i, (src, rt, rb, c, adcn) in enumerate(divs):
        x = 22.86 + 38.1 * i
        P[(rt, 1)] = (x, 15.24, 0, False)
        P[(rb, 1)] = (x, 27.94, 0, False)
        P[(c, 1)] = (x + 7.62, 27.94, 0, False)
        top_b = pin(P, rt, '2')
        bot_t = pin(P, rb, '1')
        node = (x, 21.59)
        L['wires'] += [[top_b, node], [node, bot_t]]
        ct = pin(P, c, '1')
        L['wires'].append([node, (ct[0], node[1]), ct])
        L['wires'].append([(ct[0], node[1]), (ct[0] + 7.62, node[1])])
        L['junctions'] += [node, (ct[0], node[1])]
        L['labels'].append((adcn, ct[0] + 7.62, node[1], 0))
    P[('J11', 1)] = (7.62, 12.7, 0, 'y')
    L['notes'] = [(0, 40.0, 'ESP32 ADC1 inputs (GPIO36/39/34/35).  AO readback /2, AI1 /2 (~6 V), AI2 /11 (~34 V).')]
    L['height'] = 46
    return L


# --------------------------------------------------------------------- LoRa
def lora(pin):
    L = _new()
    P = L['parts']
    P[('U10', 1)] = (40.64, 25.4, 0, False)
    P[('J13', 1)] = (66.04, 15.24, 0, 'y')
    P[('C9', 1)] = (55.88, 7.62, 0, False)
    P[('R3', 1)] = (21.59, 27.94, 90, False)
    ant = pin(P, 'U10', '1')
    j = pin(P, 'J13', '1')
    L['wires'].append(hvh(ant, j, (ant[0] + j[0]) / 2))
    L['labels'].append(('LORA_ANT', ant[0] + 1.27, ant[1], 0))
    L['wired'].add('LORA_ANT')
    nss = pin(P, 'U10', '15')
    r3 = pin(P, 'R3', '2')
    L['wires'].append([r3, nss])
    L['labels'].append(('LORA_NSS', nss[0] - 5.08, nss[1], 0))
    L['height'] = 50
    return L


def build(pin):
    return {'sense': sense(pin), 'out': outputs(pin), 'led': leds(pin), 'jig': jig(pin),
            'pulse': pulse(pin), 'adc': adc(pin), 'lora': lora(pin)}
