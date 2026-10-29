"""Sourcing BOM: groups design.PARTS and attaches the India supplier listing checked on 2026-10-21.

python3 tools/gen_bom.py  ->  out/fab/TestJig-BOM-sourcing.csv
Prices are per listing unit (often a pack), incl. GST, as shown on the site that day.
"""
import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import design  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
Q = 'https://quartzcomponents.com/products/'
R = 'https://robu.in/product/'

# key: value as written in design.py (or a lib_id for parts whose value is free text)
SRC = {
    '74HC595': ('Quartz', '74HC595 NXP SOIC-16', 'Rs 9 each', Q + '74hc595-8-bit-shift-register-icnxp-soic-16-smd-package'),
    '74HC165': ('Quartz', '74HC165 SOIC-16', 'Rs 14 each', Q + '74hc165-ic-8-bit-parallel-in-serial-out-shift-register-ic-smd-package'),
    'MCP6002': ('Quartz', 'MCP6002-I/SN SOIC-8', 'Rs 44 each', Q + 'mcp6002-i-sn-microchip-dualoperational-amplifier-ic-soic-8-package'),
    'MMBT3904': ('Quartz', 'MMBT3904 SOT-23', 'Rs 3 each', Q + 'mmbt3904-npn-transistor-sot-23-smd-package'),
    'BC807-40': ('Quartz', 'BC807-40 SOT-23', 'Rs 8 / 5 pcs', Q + 'bc807-40-45-v-500-ma-smd-sot-23-package-pnp-general-purpose-transistors-pack-of-5-pieces'),
    'BAT54WS': ('Quartz', 'BAT54WS SOD-323', 'Rs 19 / 10 pcs', Q + 'bat54ws-small-signal-schottky-diode-sod-323-smd-package'),
    'SRV05-4': ('Quartz', 'SRV05-4 SOT-23-6', 'Rs 7 each', Q + 'srv05-4-esd-protection-array-diode-sot-23-6l-smd-package'),
    'Green': ('Quartz', 'Green LED 1206', 'Rs 36 / 50 pcs', Q + 'green-led-smd-1206-package-pack-of-50'),
    'Red': ('Quartz', 'Red LED 1206', 'Rs 35 / 50 pcs', Q + 'red-led-smd-1206-package-pack-of-50'),
    'Yellow': ('Quartz', 'Yellow LED 1206', 'Rs 39 / 50 pcs', Q + 'yellow-led-smd-1206-package-pack-of-50'),
    'Blue': ('Quartz', 'Blue LED 1206', 'Rs 32 / 50 pcs', Q + 'blue-led-smd-1206-package-pack-of-50'),
    '10k': ('Quartz', '10k 1% 0805', 'Rs 13 / 20 pcs', Q + '10k-ohm-103-1-smd-resistor-0805-pack-of-20-pieces'),
    '10k 1%': ('Quartz', '10k 1% 0805', 'Rs 13 / 20 pcs', Q + '10k-ohm-103-1-smd-resistor-0805-pack-of-20-pieces'),
    '1k': ('Quartz', '1k 1% 0805', 'Rs 18 / 20 pcs', Q + '1k-ohm-102-1-smd-resistor-0805-pack-of-20-pieces'),
    '330': ('Quartz', '330R 1% 0805 (Yageo)', 'Rs 6 / 20 pcs', Q + 'yageo-330-ohm-0805-package-1-8w-smd-resistor-1-tolerance-pack-of-20-pieces'),
    '470': ('Quartz', '470R 1% 0805 (Yageo)', 'Rs 6 / 20 pcs', Q + 'yageo-470-ohm-0805-package-1-8w-smd-resistor-1-tolerance-pack-of-20-pieces'),
    '150R': ('Quartz', '150R 1% 0805 (Yageo)', 'Rs 6 / 20 pcs', Q + 'yageo-150-ohm-0805-package-1-8w-smd-resistor-1-tolerance-pack-of-20-pieces'),
    '100k': ('Quartz', '100k 1% 0805 (Yageo)', 'Rs 6 / 20 pcs', Q + 'yageo-100k-ohm-0805-package-1-8w-smd-resistor-1-tolerance-pack-of-20-pieces'),
    '4k7': ('Quartz', '4k7 5% 0805', 'Rs 8 / 20 pcs', Q + '4-7k-4k7-ohm-472-5-smd-resistor-0805-pack-of-20-pieces'),
    '47R 1%': ('Quartz', '47R 1% 0805', 'Rs 10 / 20 pcs', Q + '43-ohm-43r-1-smd-resistor-0805-pack-of-20-pieces-copy'),
    '100nF': ('Quartz', '100nF 50V X7R 0805 (Kemet)', 'Rs 26 / 20 pcs', Q + 'kemet-100nf-50v-0805-smd-pack-x7r-multilayer-ceramic-capacitor-10-tolerence-pack-of-20'),
    '1uF': ('Quartz', '1uF 50V X7R 0805', 'Rs 15 / 5 pcs', Q + '1uf-1000nf-50v-0805-x7r-smd-capacitor-pack-of-5-pieces'),
    '10uF': ('Quartz', '10uF 10V X5R 0805 (Kemet)', 'Rs 45 / 20 pcs', Q + 'kemet-10uf-50v-0805-smd-pack-x5r-multilayer-ceramic-capacitor-20-tolerence-pack-of-20'),
    'XH3': ('Quartz', '3-pin JST XH male (vertical)', 'Rs 3 each', Q + '3-pin-jst-xh-male-connector-5-24mm-pitch'),
    'XH4': ('Quartz', '4-pin JST XH male (vertical)', 'Rs 2 each', Q + '4-pin-jst-xh-male-connector-5-24mm-pitch'),
    'XH5': ('Quartz', '5-pin JST XH male (vertical)', 'Rs 2 each', Q + '5-pin-jst-xh-male-connector-5-24mm-pitch'),
    'XH6': ('Robu', 'JST B6B-XH-A (genuine), SKU R174907', 'Rs 13 each', 'https://robu.in/?s=B6B-XH-A&post_type=product'),
    'SW_DIP_x04': ('Robu', '4-pos DIP switch THT 2.54 mm (SHOU HAN CJPB-H), SKU R235110', 'Rs 36 each',
                   'https://robu.in/?s=4+position+dip+switch&post_type=product'),
    'SMA': ('Quartz', 'SMA female straight edge mount, 1.6 mm PCB', 'Rs 22 each', Q + 'sma-female-straight-edge-pcb-mount-connector-for-1-6mm-pcb'),
    'ESP32_DevKit_30pin': ('Quartz', 'ESP32 30-pin CP2102 DevKit', 'Rs 375 each', Q + 'esp32-30-pin-development-board-with-wi-fi-and-bluetooth'),
    'SELFTEST': ('Quartz', 'Male berg strip 2.54 mm (cut 2 pins) + jumper cap', '-', ''),
    'Ai-Thinker-Ra-01': ('WM1 source', 'ISC-SX1262-B / Ra-01SH, same module as WM1-Pro', '-',
                         'Not checked on Quartz/Robu; Quartz LRTC-1262 is SX1262 but a different pinout'),
}

EXTRA = [  # mechanical / harness items not on the board netlist
    ('-', 2, '15-pin female header 2.54 mm (cut from 40-pin strip)', '-', 'Quartz', '40-pin female berg strip',
     'Rs 10 each', Q + '40-pin-straight-female-berg-strips'),
    ('-', 4, 'M3 x 10 mm standoff + screw', '-', 'any', '', '', ''),
]


def key(p):
    lib = p['lib'].split(':')[1]
    if lib == 'LED':
        return p['value'].split()[-1]
    if lib in ('Conn_01x03', 'Conn_01x04', 'Conn_01x05', 'Conn_01x06') and 'XH' in p['fp']:
        return 'XH' + lib[-1]
    if lib == 'Conn_Coaxial':
        return 'SMA'
    if lib in ('SW_DIP_x04', 'ESP32_DevKit_30pin', 'Ai-Thinker-Ra-01'):
        return lib
    return p['value']


groups = collections.OrderedDict()
for p in design.PARTS:
    if p['ref'].startswith(('#', 'H')):
        continue
    k = (key(p), p['value'], p['fp'].split(':')[-1])
    groups.setdefault(k, []).append(p['ref'])


def refsort(r):
    import re
    m = re.match(r'([A-Z]+)(\d+)', r)
    return (m.group(1), int(m.group(2)))


os.makedirs(os.path.join(ROOT, 'out', 'fab'), exist_ok=True)
out = os.path.join(ROOT, 'out', 'fab', 'TestJig-BOM-sourcing.csv')
missing = []
with open(out, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['References', 'Qty', 'Value', 'Footprint', 'Supplier', 'Listing', 'Price (incl GST)', 'Link'])
    for (k, val, fp), refs in sorted(groups.items(), key=lambda kv: refsort(sorted(kv[1], key=refsort)[0])):
        src = SRC.get(k)
        if not src:
            missing.append(k)
            src = ('?', '', '', '')
        w.writerow([', '.join(sorted(refs, key=refsort)), len(refs), val, fp] + list(src))
    for row in EXTRA:
        w.writerow(row)
print('wrote', out, '| groups:', len(groups), '| unsourced:', missing or 'none')
