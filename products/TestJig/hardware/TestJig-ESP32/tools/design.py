"""Single source of truth for the TestJig-ESP32 netlist.

Each part: ref, lib (symbol lib_id), value, fp (footprint), nets {pin: net},
block (schematic block id), rot (schematic rotation), units (for multi-unit parts).
Pins not listed in `nets` get a no-connect flag in the schematic.
"""

R0805 = 'Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder'
C0805 = 'Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder'
SOIC16 = 'Package_SO:SOIC-16_3.9x9.9mm_P1.27mm'
SOIC8 = 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
SOT23 = 'Package_TO_SOT_SMD:SOT-23'
LED1206 = 'LED_SMD:LED_1206_3216Metric_Pad1.42x1.75mm_HandSolder'
SOT23_6 = 'Package_TO_SOT_SMD:SOT-23-6'
SOD323 = 'Diode_SMD:D_SOD-323_HandSoldering'


def XH(n):
    return 'Connector_JST:JST_XH_B%dB-XH-A_1x%02d_P2.50mm_Vertical' % (n, n)


PARTS = []


def part(ref, lib, value, fp, nets, block, rot=0, units=None, **kw):
    p = dict(ref=ref, lib=lib, value=value, fp=fp,
             nets={str(k): v for k, v in nets.items()}, block=block, rot=rot,
             units=units or [1])
    p.update(kw)
    PARTS.append(p)
    return p


def R(ref, val, a, b, block, fp=R0805):
    part(ref, 'Device:R', val, fp, {1: a, 2: b}, block, rot=90)


def C(ref, val, a, b, block, fp=C0805):
    part(ref, 'Device:C', val, fp, {1: a, 2: b}, block, rot=0)


# Former 4x0603 resistor arrays, now discrete 0805 (hand soldering). Group -> first R number.
RN_BASE = {'RN1': 101, 'RN2': 105, 'RN3': 109, 'RN4': 113,      # relay sense series 1k
           'RN5': 121, 'RN6': 125, 'RN7': 129, 'RN8': 133,      # relay sense pull-ups 10k
           'RN9': 141, 'RN10': 145, 'RN11': 149, 'RN12': 153,   # DUT output series 330R
           'RN13': 161, 'RN14': 165,                             # LED 470R
           'RN15': 171, 'RN16': 175}                             # jig input pull-ups 10k


def RN(group, val, left, right, block):
    """Four discrete resistors left[i] -- R -- right[i], numbered from RN_BASE[group]."""
    for i in range(4):
        R('R%d' % (RN_BASE[group] + i), val, left[i], right[i], block)


# ---------------------------------------------------------------- MCU (ESP32 30-pin DevKit)
DEVKIT_PINS = {
    1: ('3V3', '+3V3'), 2: ('GND', 'GND'), 3: ('IO15', 'LORA_DIO1'), 4: ('IO2', 'SR_LOAD'),
    5: ('IO4', 'SR_OE'), 6: ('IO16', 'PO1'), 7: ('IO17', 'PO2'), 8: ('IO5', 'LORA_NSS'),
    9: ('IO18', 'SPI_SCK'), 10: ('IO19', 'SPI_MISO'), 11: ('IO21', 'PI1'), 12: ('RX0', None),
    13: ('TX0', None), 14: ('IO22', 'PI2'), 15: ('IO23', 'SPI_MOSI'), 16: ('EN', None),
    17: ('VP/IO36', 'ADC_AO1'), 18: ('VN/IO39', 'ADC_AO2'), 19: ('IO34', 'ADC_AI1'),
    20: ('IO35', 'ADC_AI2'), 21: ('IO32', 'PWM_AO1'), 22: ('IO33', 'PWM_AO2'),
    23: ('IO25', 'LORA_RST'), 24: ('IO26', 'SR_IN'), 25: ('IO27', 'LORA_BUSY'),
    26: ('IO14', 'SR_LATCH'), 27: ('IO12', 'SR_CLK'), 28: ('IO13', 'SR_DATA'),
    29: ('GND', 'GND'), 30: ('VIN', '+5V'),
}
part('U1', 'TestJig:ESP32_DevKit_30pin', 'ESP32 DevKit 30p',
     'TestJig:ESP32_DevKit_30pin_W25.4mm',
     {k: v[1] for k, v in DEVKIT_PINS.items() if v[1]}, 'mcu')
R('R1', '10k', 'SR_OE', '+3V3', 'mcu')          # 595 outputs Hi-Z during boot
R('R2', '10k', 'SR_CLK', 'GND', 'mcu')          # GPIO12 strap must be low at boot
R('R3', '10k', 'LORA_NSS', '+3V3', 'mcu')       # GPIO5 strap high, LoRa deselected
C('C2', '10uF', '+5V', 'GND', 'mcu')
C('C1', '10uF', '+3V3', 'GND', 'mcu')
part('#FLG1', 'power:PWR_FLAG', 'PWR_FLAG', '', {1: 'GND'}, 'mcu')

# ---------------------------------------------------------------- 74HC595 chain (DUT inputs + LEDs)
# Datasheet pins: QA=15 QB..QH=1..7 QH'=9 SER=14 SRCLK=11 RCLK=12 /OE=13 /SRCLR=10
Q595 = [15, 1, 2, 3, 4, 5, 6, 7]


def hc595(ref, ser, outs, qh_next, block):
    nets = {14: ser, 11: 'SR_CLK', 12: 'SR_LATCH', 13: 'SR_OE', 10: '+3V3',
            16: '+3V3', 8: 'GND'}
    if qh_next:
        nets[9] = qh_next
    for pin, net in zip(Q595, outs):
        nets[pin] = net
    part(ref, '74xx:74HC595', '74HC595', SOIC16, nets, block)


hc595('U2', 'SR_DATA', ['DO%d' % i for i in range(1, 9)], 'SR_D2', 'out')
hc595('U3', 'SR_D2', ['DO%d' % i for i in range(9, 17)], 'SR_D3', 'out')
hc595('U4', 'SR_D3', ['LEDDRV%d' % i for i in range(1, 9)], None, 'led')
for i, c in enumerate(['C3', 'C4', 'C5']):
    C(c, '100nF', '+3V3', 'GND', ['out', 'out', 'led'][i])

for k in range(4):
    base = 4 * k + 1
    RN('RN%d' % (9 + k), '330', ['DO%d' % (base + i) for i in range(4)],
       ['DUT_OUT%d' % (base + i) for i in range(4)], 'out')
    part('J%d' % (5 + k), 'Connector_Generic:Conn_01x05', 'DUT_IN %d-%d' % (base, base + 3), XH(5),
         {1: 'GND', 2: 'DUT_OUT%d' % base, 3: 'DUT_OUT%d' % (base + 1),
          4: 'DUT_OUT%d' % (base + 2), 5: 'DUT_OUT%d' % (base + 3)}, 'out')

LED_NAMES = ['PASS', 'FAIL', 'BUSY', 'LORA', 'USER1', 'USER2', 'USER3', 'USER4']
LED_COLS = ['Green', 'Red', 'Yellow', 'Blue', 'Green', 'Green', 'Green', 'Green']
RN('RN13', '470', ['LEDDRV%d' % i for i in range(1, 5)], ['LEDA%d' % i for i in range(1, 5)], 'led')
RN('RN14', '470', ['LEDDRV%d' % i for i in range(5, 9)], ['LEDA%d' % i for i in range(5, 9)], 'led')
for i in range(8):
    part('D%d' % (7 + i), 'Device:LED', '%s %s' % (LED_NAMES[i], LED_COLS[i]), LED1206,
         {1: 'GND', 2: 'LEDA%d' % (i + 1)}, 'led', rot=0)

# ---------------------------------------------------------------- 74HC165 chain (relay sense + jig inputs)
# Datasheet pins: /PL=1 CP=2 D4..D7=3..6 /Q7=7 Q7=9 DS=10 D0..D3=11..14 /CE=15
D165 = [11, 12, 13, 14, 3, 4, 5, 6]


def hc165(ref, ds, ins, q7, block):
    nets = {1: 'SR_LOAD', 2: 'SR_CLK', 15: 'GND', 10: ds, 9: q7, 16: '+3V3', 8: 'GND'}
    for pin, net in zip(D165, ins):
        nets[pin] = net
    part(ref, '74xx:74HC165', '74HC165', SOIC16, nets, block)


hc165('U5', 'SR_Q6', ['RS%d' % i for i in range(1, 9)], 'SR_IN', 'sense')
hc165('U6', 'SR_Q7', ['RS%d' % i for i in range(9, 17)], 'SR_Q6', 'sense')
hc165('U7', 'GND', ['DIP1', 'DIP2', 'DIP3', 'DIP4', 'START', 'LID', 'SPARE_IN1', 'SPARE_IN2'],
      'SR_Q7', 'jig')
for i, c in enumerate(['C6', 'C7', 'C8']):
    C(c, '100nF', '+3V3', 'GND', ['sense', 'sense', 'jig'][i])

for k in range(4):
    base = 4 * k + 1
    part('J%d' % (1 + k), 'Connector_Generic:Conn_01x05', 'RLY_SENSE %d-%d' % (base, base + 3), XH(5),
         {1: 'GND', 2: 'RLY_NO%d' % base, 3: 'RLY_NO%d' % (base + 1),
          4: 'RLY_NO%d' % (base + 2), 5: 'RLY_NO%d' % (base + 3)}, 'sense')
    RN('RN%d' % (1 + k), '1k', ['RLY_NO%d' % (base + i) for i in range(4)],
       ['RS%d' % (base + i) for i in range(4)], 'sense')
    RN('RN%d' % (5 + k), '10k', ['RS%d' % (base + i) for i in range(4)], ['+3V3'] * 4, 'sense')

RN('RN15', '10k', ['DIP1', 'DIP2', 'DIP3', 'DIP4'], ['+3V3'] * 4, 'jig')
RN('RN16', '10k', ['START', 'LID', 'SPARE_IN1', 'SPARE_IN2'], ['+3V3'] * 4, 'jig')
part('SW1', 'Switch:SW_DIP_x04', 'DIP-4',
     'Button_Switch_THT:SW_DIP_SPSTx04_Slide_9.78x12.34mm_W7.62mm_P2.54mm',
     {1: 'DIP1', 2: 'DIP2', 3: 'DIP3', 4: 'DIP4', 5: 'GND', 6: 'GND', 7: 'GND', 8: 'GND'}, 'jig')
part('J12', 'Connector_Generic:Conn_01x06', 'JIG_CTRL', XH(6),
     {1: 'START', 2: 'LID', 3: 'SPARE_IN1', 4: 'SPARE_IN2', 5: 'GND', 6: '+3V3'}, 'jig')

# ---------------------------------------------------------------- Pulse I/O
part('J9', 'Connector_Generic:Conn_01x04', 'PULSE IN', XH(4),
     {1: 'PULSE_IN1', 2: 'PULSE_IN2', 3: 'GND', 4: '+3V3'}, 'pulse')        # left wall (inputs)
part('J14', 'Connector_Generic:Conn_01x03', 'PULSE OUT', XH(3),
     {1: 'PULSE_OUT1', 2: 'PULSE_OUT2', 3: 'GND'}, 'pulse')                  # right wall (outputs)
R('R4', '330', 'PO1', 'PULSE_OUT1', 'pulse')
R('R5', '330', 'PO2', 'PULSE_OUT2', 'pulse')
R('R6', '1k', 'PULSE_IN1', 'PI1', 'pulse')
R('R7', '1k', 'PULSE_IN2', 'PI2', 'pulse')
R('R8', '10k', 'PULSE_IN1', '+3V3', 'pulse')
R('R9', '10k', 'PULSE_IN2', '+3V3', 'pulse')
part('U11', 'Power_Protection:SRV05-4', 'SRV05-4', SOT23_6,
     {1: 'PULSE_OUT1', 3: 'PULSE_OUT2', 4: 'PULSE_IN1', 6: 'PULSE_IN2', 2: 'GND', 5: '+3V3'}, 'pulse')

# ---------------------------------------------------------------- ESP32 ADC front-ends
R('R10', '10k', 'AO1', 'ADC_AO1', 'adc')
R('R11', '10k', 'ADC_AO1', 'GND', 'adc')
R('R12', '10k', 'AO2', 'ADC_AO2', 'adc')
R('R13', '10k', 'ADC_AO2', 'GND', 'adc')
R('R14', '10k', 'AI1', 'ADC_AI1', 'adc')
R('R15', '10k', 'ADC_AI1', 'GND', 'adc')
R('R16', '100k', 'AI2', 'ADC_AI2', 'adc')
R('R17', '10k', 'ADC_AI2', 'GND', 'adc')
for c, net in zip(['C16', 'C17', 'C18', 'C19'], ['ADC_AO1', 'ADC_AO2', 'ADC_AI1', 'ADC_AI2']):
    C(c, '100nF', net, 'GND', 'adc')
part('J11', 'Connector_Generic:Conn_01x03', 'AIN (AI1 /2, AI2 /11)', XH(3),
     {1: 'AI1', 2: 'AI2', 3: 'GND'}, 'adc')

# ---------------------------------------------------------------- 4-20 mA sources (5 V, MCP6002)


def ao(n, U, Qn, Qp, D, rb, cb, JP):
    p = 'AO%d_' % n
    R('R%d' % rb, '10k', 'PWM_AO%d' % n, p + 'F1', 'ao%d' % n)
    C('C%d' % cb, '1uF', p + 'F1', 'GND', 'ao%d' % n)
    R('R%d' % (rb + 1), '10k', p + 'F1', p + 'VSET', 'ao%d' % n)
    C('C%d' % (cb + 1), '1uF', p + 'VSET', 'GND', 'ao%d' % n)
    part(U, 'Amplifier_Operational:MCP6002-xSN', 'MCP6002', SOIC8,
         {1: p + 'U1', 2: p + 'QE', 3: p + 'VSET', 4: 'GND', 5: p + 'A', 6: p + 'B', 7: p + 'U2',
          8: '+5V'}, 'ao%d' % n, units=[1, 2, 3])
    R('R%d' % (rb + 2), '1k', p + 'U1', p + 'QB1', 'ao%d' % n)
    part(Qn, 'Transistor_BJT:MMBT3904', 'MMBT3904', SOT23, {1: p + 'QB1', 2: p + 'QE', 3: p + 'A'},
         'ao%d' % n)
    R('R%d' % (rb + 3), '10k 1%', p + 'QE', 'GND', 'ao%d' % n)
    R('R%d' % (rb + 4), '4k7', '+5V', p + 'A', 'ao%d' % n)
    R('R%d' % (rb + 5), '1k', p + 'U2', p + 'QB2', 'ao%d' % n)
    R('R%d' % (rb + 6), '47R 1%', '+5V', p + 'B', 'ao%d' % n)
    part(Qp, 'Transistor_BJT:BC807', 'BC807-40', SOT23, {1: p + 'QB2', 2: p + 'B', 3: p + 'QC'},
         'ao%d' % n)
    part(D, 'Device:D_Schottky', 'BAT54WS', SOD323, {1: 'AO%d' % n, 2: p + 'QC'}, 'ao%d' % n, rot=0)
    part(JP, 'Connector_Generic:Conn_01x02', 'SELFTEST', 'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',
         {1: 'AO%d' % n, 2: p + 'LOAD'}, 'ao%d' % n)
    R('R%d' % (rb + 7), '150R', p + 'LOAD', 'GND', 'ao%d' % n)


ao(1, 'U8', 'Q1', 'Q2', 'D1', 20, 12, 'JP1')
ao(2, 'U9', 'Q3', 'Q4', 'D2', 30, 14, 'JP2')
C('C10', '100nF', '+5V', 'GND', 'ao1')
C('C11', '100nF', '+5V', 'GND', 'ao2')
C('C20', '10uF', '+5V', 'GND', 'ao2')
part('J10', 'Connector_Generic:Conn_01x03', '4-20mA OUT', XH(3), {1: 'AO1', 2: 'AO2', 3: 'GND'}, 'ao2')

# ---------------------------------------------------------------- LoRa sniffer (SX1262, WM1-Pro pinout)
part('U10', 'RF_Module:Ai-Thinker-Ra-01', 'Ra-01SH (SX1262)',
     'RF_Module:Ai-Thinker-Ra-01-LoRa',
     {1: 'LORA_ANT', 2: 'GND', 9: 'GND', 16: 'GND', 3: '+3V3', 4: 'LORA_RST', 6: 'LORA_DIO1',
      10: 'LORA_BUSY', 12: 'SPI_SCK', 13: 'SPI_MISO', 14: 'SPI_MOSI', 15: 'LORA_NSS'}, 'lora')
part('J13', 'Connector:Conn_Coaxial', 'SMA', 'Connector_Coaxial:SMA_Samtec_SMA-J-P-H-ST-EM1_EdgeMount',
     {1: 'LORA_ANT', 2: 'GND'}, 'lora')
C('C9', '100nF', '+3V3', 'GND', 'lora')

# ---------------------------------------------------------------- Mounting
for i in range(2):   # top-centre and bottom-centre bosses of the box, 92 mm apart
    part('H%d' % (i + 1), 'Mechanical:MountingHole', 'M3', 'MountingHole:MountingHole_3.2mm_M3', {}, 'mech')

BLOCKS = [
    ('mcu', 'ESP32 DevKit 30-pin  (USB powered: VIN = USB 5V, 3V3 from DevKit LDO)'),
    ('sense', 'DUT RELAY SENSE  -  COM + NO, 4x JST-XH5 (pin1 = COM/GND). Relay ON -> NO closed -> reads 0'),
    ('jig', 'JIG INPUTS  -  4-way DIP, START, LID, SPARE1/2 on J12 (3rd 74HC165)'),
    ('out', 'DUT INPUT DRIVE  -  74HC595 @3.3V, 4x JST-XH5 (pin1 = GND)'),
    ('led', 'JIG LEDs  -  3rd 74HC595'),
    ('pulse', 'PULSE I/O  -  J14 out (LEDC/RMT), J9 in (PCNT)'),
    ('adc', 'ESP32 ADC1 FRONT-ENDS  -  AO readback /2, AI1 /2, AI2 /11'),
    ('ao1', '4-20 mA SOURCE #1  -  Iout = Vset x 10 mA/V (0.4 V = 4 mA, 2.0 V = 20 mA), 5 V, load <= ~160R'),
    ('ao2', '4-20 mA SOURCE #2'),
    ('lora', 'LoRa SNIFFER  -  SX1262 module (WM1-Pro pins, except DIO1 on IO15)'),
    ('mech', 'MECHANICAL'),
]
