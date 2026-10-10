"""ASC-AHT20: the T/RH sensor stick that hangs outside the Mini's box on a 4-core cable.

48 x 6 mm, so four of them fill the 96 x 12 strip under the four Minis on the 100 x 100 panel
(two rows of two; every V-cut runs edge to edge). The cable is soldered into J1 at one end
(3V3 GND SCL SDA, the same order as the Mini's I2C terminal); the AHT20 sits at the other end,
~35 mm away, with no copper pour around it, so heat from the hand-held cable end reaches the
sensor slowly. Students breathe on the tip to see T and RH move. I2C address 0x38; the Mini's
4.7k pull-ups serve, so there are none here.

    DESIGN=design_aht20 python3 tools/gen_sch.py
    DESIGN=design_aht20 $KP tools/gen_pcb.py ; DESIGN=design_aht20 $KP tools/route.py
"""

PROJECT = "ASC-AHT20"
TITLE = "ASC Student Kit - AHT20 T/RH sensor stick rev A"
DATE, REV, PAPER = "2026-10-10", "A", "A4"
COMMENT = "48 x 6 mm stick; four per 100 x 100 panel, under the four Mini-C3 boards"
POWER_NETS = {"GND", "+3V3"}

P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

NOTES = []
def note(text, x, y):
    NOTES.append((text, x, y))

note("AHT20 T/RH sensor board. Cable to the Mini's I2C terminal: 3V3 GND SCL SDA.", 30.48, 30.48)
note("10 uF decoupling at the sensor (datasheet 5.1). Pull-ups are on the Mini (4.7k to 3V3).", 30.48, 34.29)
part("U1", "ASC:AHT20", "AHT20", "ASC:Aosong_AHT20_DFN-6_3x3mm", (127.0, 76.2),
     {"VDD": "+3V3", "GND": "GND", "SCL": "SCL", "SDA": "SDA"})
part("C1", "Device:C", "10u", "Capacitor_SMD:C_0805_2012Metric", (152.4, 76.2), {"1": "+3V3", "2": "GND"})
part("J1", "Connector_Generic:Conn_01x04", "CABLE", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
     (76.2, 76.2), {"1": "+3V3", "2": "GND", "3": "SCL", "4": "SDA"})

PWR_FLAGS = [("GND", (101.6, 106.68)), ("+3V3", (114.3, 106.68))]

BOARD_W, BOARD_H, CORNER_R = 48.0, 6.0, 0.0
HOLES = []
SLOTS = []

PLACE = {
    "J1": (6.4, 3.0, 90),
    "C1": (40.6, 3.0, 0),
    "U1": (45.0, 3.0, 0),
}
NO_POUR = [(30.0, 0.0, 48.0, 6.0)]
PREROUTE = []
POUR_SOLID = True                           # tiny board: pads join the pour solidly (no thermal spokes)
NO_TRACK = []
NETCLASSES = {"Power": (0.4, 0.2, ["+3V3", "GND"])}

def silk(T):
    for num, name in {"1": "3V3", "2": "GND", "3": "SCL", "4": "SDA"}.items():
        x, y = T.pad_xy("J1", num)
        T.text(name, x, 5.0, 0.8)
    T.text("ASC T/RH", 21.0, 1.6, 0.8, bold=True)
    T.text("BREATHE HERE >", 32.0, 4.4, 0.8, bold=True)
    T.text("AHT20 0x38 rev A", 24.0, 3.0, 0.8, layer="B", bold=True)

def netname(n):
    return n if n in POWER_NETS else "/" + n
