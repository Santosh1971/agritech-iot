"""WM-Mini C3 rev A: the single source of truth for parts, nets and placement.

A larger FG1 (2026-10-27): an ESP32-C3 Super Mini (plug-in, USB-C powered) drives six SRD relays
through a ULN2003A: K1 PUMP and K2 DOSER switch 230 V, K3-K6 switch 12/24 VAC valves (one common).
Flow input (Hall sensor), DS1307 RTC with a CR2032, seven LEDs seen through the lid.
Spec and layout plan: ../SPEC.md (draft 0.3) and the WM-Mini C3 Plan page.

Board 99 x 48 mm, two per 99 x 98 panel. All cables (USB, flow, valves, doser, pump) leave from
the bottom edge. Two M3 holes 92 mm apart on the centre line, as the ESP32 test jig (same IP65 box).

Used by gen_sch.py (schematic + netlist, system python3) and gen_pcb.py / route.py (KiCad's python).
"""

PROJECT = "WM-MiniC3"
TITLE = "WM-Mini C3 - pump, doser and 4-valve irrigation controller"
DATE, REV, PAPER = "2026-10-27", "A", "A3"
COMMENT = "99 x 48 mm, 2 per panel; K1/K2 230 V, K3-K6 12/24 VAC"
POWER_NETS = {"GND", "+3V3", "+5V"}

# ---- footprints ---------------------------------------------------------------------
R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
LED1206 = "LED_SMD:LED_1206_3216Metric"          # 1206 LEDs: bigger pads for hand soldering (house style)
SRD = "ASC:Relay_SPDT_SANYOU_SRD_Outline"   # KiCad's SRD Form C footprint, silk reduced to the outline (labels go inside)
MSTBA = "ASC:Pluggable_2EDGR_5.08_1x0%d_Horizontal"   # 5.08 mm pluggable, stackable (2EDGR); MSTBA pads, as WM1's bottom edge

# ---- parts ----------------------------------------------------------------------------
# part(ref, lib_id, value, footprint, (x, y) on the A3 sheet in mm, {pin number or name: net})
P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

NOTES = []
def note(text, x, y):
    NOTES.append((text, x, y))

BOXES = []      # schematic block frames: (title, x0, y0, x1, y1)
def box(title, x0, y0, x1, y1):
    BOXES.append((title, x0, y0, x1, y1))

WIRES, POWERS, LABELS, NOLABEL = [], [], [], []
def wire(*pts):
    WIRES.append(list(pts))

# ================================================================ 1. MCU and power
box("1  MCU AND POWER", 12.7, 22.86, 132.08, 129.54)
note("ESP32-C3 Super Mini, plugged into 2 x 8-pin sockets (plugs in after the board is screwed down).", 17.78, 30.48)
note("USB-C on the module powers the whole board: 5 V / 2 A adapter. 5V pin = USB VBUS,", 17.78, 34.29)
note("3V3 pin = the module's LDO (pull-ups, flow input, flow LED). C1 rides out relay switching.", 17.78, 38.1)
note("IO8 = on-module blue LED (WiFi status). IO9 = on-module BOOT button (setup after boot).", 17.78, 113.03)
note("IO2 = strapping pin, left unused (as Mini-C3).", 17.78, 116.84)
note("Relay drives: IO20 pump, IO10 doser, IO1 V1, IO0 V2, IO3 V3, IO4 V4.", 17.78, 120.65)
part("U1", "ASC:ESP32-C3_SuperMini", "ESP32-C3 Super Mini", "ASC:ESP32-C3_SuperMini_Socket", (66.04, 78.74),
     {"5V": "+5V", "GND": "GND", "3V3": "+3V3",
      "IO20": "RLY_PUMP", "IO10": "RLY_DOSER", "IO1": "RLY_V1", "IO0": "RLY_V2", "IO3": "RLY_V3", "IO4": "RLY_V4",   # as routed on the PCB
      "IO5": "FLOW", "IO6": "SDA", "IO7": "SCL", "IO21": "LED_FLOW"}, desc=(86.36, 99.06))
part("C1", "Device:C_Polarized", "1000u 16V", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", (45.72, 53.34),
     {"1": "+5V", "2": "GND"}, desc=(48.26, 53.34))
wire(("U1", "5V"), (63.5, 45.72)); POWERS.append(("+5V", (63.5, 45.72), 0))
wire((63.5, 48.26), (45.72, 48.26), ("C1", "1"))
wire(("U1", "3V3"), (68.58, 55.88), (76.2, 55.88), (76.2, 45.72)); POWERS.append(("+3V3", (76.2, 45.72), 0))

# ================================================================ 2. RTC
box("2  REAL-TIME CLOCK", 137.16, 22.86, 259.08, 129.54)
note("DS1307 at I2C 0x68, on 5 V (it needs 4.5-5.5 V). Pull-ups to 3V3: it reads 3.3 V as high.", 142.24, 30.48)
note("Backup: CR2032 in a vertical holder (measure the holder's pins before ordering).", 142.24, 34.29)
note("Crystal 32.768 kHz, 12.5 pF load, through-hole C26 cylinder (as Mini-C3 / FG1), lying flat.", 142.24, 38.1)
part("U2", "Timer_RTC:DS1307Z+", "DS1307Z+", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", (208.28, 78.74),
     {"VCC": "+5V", "GND": "GND", "X1": "XT1", "X2": "XT2", "VBAT": "VBAT", "SDA": "SDA", "SCL": "SCL"}, desc=(215.9, 96.52))
part("Y1", "Device:Crystal", "32.768k", "Crystal:Crystal_C26-LF_D2.1mm_L6.5mm_Horizontal", (185.42, 85.09),
     {"1": "XT2", "2": "XT1"}, rot=90, desc=(170.18, 86.36))    # pins swapped for a straight X1 track on the PCB
part("BT1", "Device:Battery_Cell", "CR2032", "ASC:Battery_CR2032_Vertical_3pin", (231.14, 68.58),
     {"1": "VBAT", "2": "GND"}, desc=(235.0, 67.31))
part("C2", "Device:C", "100n", C0805, (193.04, 54.61), {"1": "+5V", "2": "GND"}, desc=(196.85, 54.61))
part("R1", "Device:R", "4.7k", R0805, (162.56, 66.04), {"1": "+3V3", "2": "SDA"}, desc=(149.86, 66.04))
part("R2", "Device:R", "4.7k", R0805, (172.72, 66.04), {"1": "+3V3", "2": "SCL"}, desc=(175.26, 66.04))
wire(("U2", "X1"), ("Y1", "2"))
wire(("U2", "X2"), (190.5, 83.82), (190.5, 88.9), ("Y1", "1"))
LABELS += [("XT1", (189.23, 81.28), 0), ("XT2", (187.96, 88.9), 0)]
wire(("U2", "VBAT"), (208.28, 60.96), (231.14, 60.96), ("BT1", "1"))
wire(("U2", "VCC"), (205.74, 45.72)); POWERS.append(("+5V", (205.74, 45.72), 0))
wire((205.74, 48.26), (193.04, 48.26), ("C2", "1"))
wire(("U2", "SCL"), (172.72, 73.66), ("R2", "2")); LABELS.append(("SCL", (172.72, 73.66), 180))
wire(("U2", "SDA"), (162.56, 76.2), ("R1", "2")); LABELS.append(("SDA", (162.56, 76.2), 180))

# ================================================================ 3. Flow input
box("3  FLOW INPUT AND FLOW LED", 264.16, 22.86, 408.94, 129.54)
note("J1 FLOW: Hall-effect sensor (YF-S201 type) on 5 V. A 2-wire pulse meter uses SIG + GND.", 269.24, 30.48)
note("The sensor output swings to 5 V: Schottky D1 only pulls IO5 low, R3 pulls it up to 3V3.", 269.24, 34.29)
note("D2: SLCESDB5V0D5 5 V bidirectional TVS (SOD-523, hand-solder pads) on the field cable. FLOW LED on IO21.", 269.24, 38.1)
part("J1", "Connector_Generic:Conn_01x03", "FLOW", MSTBA % 3, (284.48, 76.2),
     {"1": "+5V", "2": "GND", "3": "FLOW_EXT"}, desc=(287.02, 72.39))
part("D2", "Device:D_TVS", "SLCESDB5V0D5", "ASC:D_SOD-523_HandSolder", (287.02, 100.33), {"1": "FLOW_EXT", "2": "GND"},
     rot=270, desc=(290.83, 104.14))
part("D1", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", (304.8, 96.52), {"1": "FLOW_EXT", "2": "FLOW"},
     desc=(300.99, 91.44))
part("R3", "Device:R", "10k", R0805, (317.5, 92.71), {"1": "+3V3", "2": "FLOW"}, desc=(320.04, 90.17))
part("R4", "Device:R", "1k", R0805, (350.52, 78.74), {"1": "LED_FLOW", "2": "Net-(D3-A)"}, rot=90, desc=(347.98, 72.39))
part("D3", "Device:LED", "GRN", LED1206, (365.76, 78.74), {"1": "GND", "2": "Net-(D3-A)"}, rot=180, desc=(362.71, 72.39))
wire(("J1", "1"), (271.78, 73.66), (271.78, 66.04)); POWERS.append(("+5V", (271.78, 66.04), 0))
wire(("J1", "2"), (269.24, 76.2), (269.24, 86.36)); POWERS.append(("GND", (269.24, 86.36), 0))
wire(("J1", "3"), (276.86, 78.74), (276.86, 96.52), ("D1", "K"))
LABELS.append(("FLOW_EXT", (290.83, 96.52), 0))
wire(("D1", "A"), (325.12, 96.52)); LABELS.append(("FLOW", (325.12, 96.52), 0))
NOLABEL += [("D2", "1"), ("R3", "2")]
wire(("R4", "2"), ("D3", "A"))
wire(("D3", "K"), (374.65, 78.74), (374.65, 83.82)); POWERS.append(("GND", (374.65, 83.82), 0))

# ================================================================ 4. Relay driver and outputs
box("4  RELAY DRIVER AND OUTPUTS", 12.7, 134.62, 408.94, 248.92)
note("ULN2003A: channels 1-6 straight from the GPIOs (no 74HC595). COM to +5V clamps the coil flyback. Ch 7 unused.", 17.78, 142.24)
note("Each relay LED + 1k sits across its coil: lit while the relay is energised.", 17.78, 146.05)
note("SRD-05VDC-SL-C: 1 COM, 2/5 coil, 3 NO, 4 NC (unused). K1 PUMP + K2 DOSER: 230 V, >= 5 mm to everything else (.kicad_dru).", 143.51, 142.24)
note("K3-K6: 12/24 VAC valves, one COM. K1/K2 NC is live (= COM) while off: the PCB keeps it in the 230 V class.", 143.51, 146.05)
part("U3", "Transistor_Array:ULN2003A", "ULN2003A", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm", (45.72, 190.5),
     # channel order set on the hand-placed PCB (2026-10-28): out pin 11 PUMP, 12 DOSER, 13 V3, 14 V4, 15 V2, 16 V1
     {"I1": "RLY_V2", "I2": "RLY_V3", "I3": "RLY_V4", "I4": "RLY_V1", "I5": "RLY_DOSER", "I6": "RLY_PUMP", "I7": "GND",
      "O1": "COIL_V2", "O2": "COIL_V3", "O3": "COIL_V4", "O4": "COIL_V1", "O5": "COIL_DOSER", "O6": "COIL_PUMP",
      "GND": "GND", "COM": "+5V"}, desc=(30.48, 210.82))
part("C3", "Device:C", "100n", C0805, (30.48, 167.64), {"1": "+5V", "2": "GND"}, desc=(19.05, 167.64))
RAIL_Y = 160.02
KX = [96.52, 160.02, 226.06, 266.7, 307.34, 347.98]
CHANNELS = [  # relay, LED, R, name, coil net, COM, NO
    ("K1", "D5", "R7", "PUMP", "COIL_PUMP", "PUMP_COM", "PUMP_NO"),      # LED D5 = pump, D4 = doser (hand-placed PCB)
    ("K2", "D4", "R6", "DOSER", "COIL_DOSER", "DOSER_COM", "DOSER_NO"),
    ("K3", "D7", "R9", "V2", "COIL_V2", "VALVE_COM", "V2_NO"),       # LED row left to right: D6 V1, D7 V2, D8 V3, D9 V4
    ("K4", "D8", "R10", "V3", "COIL_V3", "VALVE_COM", "V3_NO"),
    ("K5", "D6", "R8", "V1", "COIL_V1", "VALVE_COM", "V1_NO"),
    ("K6", "D9", "R11", "V4", "COIL_V4", "VALVE_COM", "V4_NO"),
]
wire((30.48, RAIL_Y), (KX[-1] - 5.08, RAIL_Y)); POWERS.append(("+5V", (30.48, RAIL_Y), 0))   # +5V rail
wire(("C3", "1"), (30.48, RAIL_Y))
wire(("U3", "COM"), (60.96, 180.34), (60.96, RAIL_Y))
for i, (k, d, r, name, coil, com, no) in enumerate(CHANNELS):
    x = KX[i]
    mains = i < 2
    flip = k in ("K3", "K4")          # V2/V3 (inverted top row on the PCB): coil on pin 5, +5V on pin 2 (Santosh, 2026-10-28)
    pins = {"5": coil, "2": "+5V"} if flip else {"5": "+5V", "2": coil}
    pins.update({"1": com, "3": no})
    part(k, "Relay:SANYOU_SRD_Form_C", "SRD-05VDC", SRD, (x, 213.36), pins,
         desc=(x - 7.62, 236.22) if mains else (x + 11.43, 212.09))
    part(r, "Device:R", "1k", R0805, (x - 12.7, 167.64), {"1": "+5V", "2": "Net-(%s-A)" % d}, desc=(x - 21.59, 166.37))
    part(d, "Device:LED", "YEL", LED1206, (x - 12.7, 177.8), {"1": coil, "2": "Net-(%s-A)" % d}, rot=90, desc=(x - 21.59, 176.53))
    wire((x - 12.7, RAIL_Y), (r, "1"))
    wire((r, "2"), (d, "A"))
    if flip:
        wire((d, "K"), (x - 12.7, 198.12), (x - 5.08, 198.12), (k, "5"))
        LABELS.append((coil, (x - 12.7, 198.12), 180))
    else:
        wire((d, "K"), (x - 12.7, 226.06), (x - 5.08, 226.06), (k, "2"))
        LABELS.append((coil, (x - 12.7, 226.06), 180))
        wire((k, "5"), (x - 5.08, RAIL_Y))
    note(name, x - 2.54, 201.93)
# pump and doser: contacts wired to their headers
for k, j, x in (("K1", "J2", KX[0]), ("K2", "J3", KX[1])):
    wire((k, "3"), (x + 7.62, 200.66), (j, "1"))
    wire((k, "1"), (x + 5.08, 228.6), (x + 12.7, 228.6), (x + 12.7, 203.2), (j, "2"))
    LABELS.append(({"K1": "PUMP_NO", "K2": "DOSER_NO"}[k], (x + 10.16, 200.66), 0))
    LABELS.append(({"K1": "PUMP_COM", "K2": "DOSER_COM"}[k], (x + 6.35, 228.6), 0))
part("J2", "Connector_Generic:Conn_01x02", "PUMP 230V", MSTBA % 2, (KX[0] + 22.86, 200.66),
     {"1": "PUMP_NO", "2": "PUMP_COM"}, desc=(KX[0] + 20.32, 210.82))
part("J3", "Connector_Generic:Conn_01x02", "DOSER 230V", MSTBA % 2, (KX[1] + 22.86, 200.66),
     {"1": "DOSER_NO", "2": "DOSER_COM"}, desc=(KX[1] + 20.32, 210.82))
# valves: one COM bus, NO pins by label to J4
VBUS_Y = 231.14
for x in KX[2:]:
    wire(("K%d" % (KX.index(x) + 1), "1"), (x + 5.08, VBUS_Y))
wire((KX[2] + 5.08, VBUS_Y), (370.84, VBUS_Y)); LABELS.append(("VALVE_COM", (370.84, VBUS_Y), 0))
part("J4", "Connector_Generic:Conn_01x05", "VALVES 24VAC", MSTBA % 5, (378.46, 177.8),
     {"1": "VALVE_COM", "2": "V1_NO", "3": "V2_NO", "4": "V3_NO", "5": "V4_NO"}, desc=(375.92, 189.23))

# ================================================================ mechanical
note("Mounting: 2 x M3, 92 mm apart on the centre line (same IP65 box bosses as the ESP32 test jig).", 17.78, 245.11)
part("H1", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", (25.4, 236.22), {})
part("H2", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", (40.64, 236.22), {})

PWR_FLAGS = [("GND", (25.4, 99.06)), ("VBAT", (231.14, 55.88))]
wire((231.14, 55.88), (231.14, 60.96))

# ---- board geometry, for gen_pcb.py ---------------------------------------------------------
BOARD_W, BOARD_H, CORNER_R = 99.0, 48.0, 0.0
HOLES = [(3.5, 24.0), (95.5, 24.0)]
SLOTS = []

# Relay axis x (the COM pin) per column, and the two rows: upright relays (rot 270) have COM and coil
# at the top and NO/NC at the bottom; the inverted top row (rot 90) has COM and coil at the bottom.
CX = {"K1": 15.1, "K2": 33.1, "K3": 54.1, "K5": 54.1, "K4": 72.1, "K6": 72.1}
UP_Y, INV_Y = 23.7, 18.8          # COM pin y: upright row / inverted row
PIN_Y = 45.5                       # connector pin row, 2.5 mm from the bottom edge; plugs overhang it

# Placement: ref -> (x, y, rotation) puts the courtyard centre on (x, y); a 4th item "o" puts the
# footprint origin there instead (relays: the COM pin; MSTBA headers: pin 1; Super Mini: pad-field centre).
PLACE = {
    # 230 V corner (left) and the valve block, see CX
    "K1": (CX["K1"], UP_Y, 270, "o"), "K2": (CX["K2"], UP_Y, 270, "o"),
    "K5": (CX["K5"], UP_Y, 270, "o"), "K6": (CX["K6"], UP_Y, 270, "o"),
    "K3": (CX["K3"], INV_Y, 90, "o"), "K4": (CX["K4"], INV_Y, 90, "o"),
    # bottom edge: pin 1 positions (pin 2.. go right, 5.08 mm)
    "J2": (9.6, PIN_Y, 0, "o"),            # NO under K1's NO pin, COM under its COM line
    "J3": (27.6, PIN_Y, 0, "o"),
    "J4": (41.2, PIN_Y, 0, "o"),           # >= 5 mm from DOSER copper
    "J1": (67.2, PIN_Y, 0, "o"),
    # Super Mini, USB-C at the bottom edge (pad 1 row 1.3 mm in from the edge)
    "U1": (89.9, 37.81, 180, "o"),
    # under the module, between its sockets: the flow input parts
    "R3": (89.9, 30.2, 0), "D1": (89.9, 33.8, 0), "D2": (89.9, 38.2, 0),
    # TOP LEFT, above the relays it drives (all copper > 5 mm from 230 V): ULN2003, the six relay LEDs and C1
    "U3": (8.0, 13.8, 90),
    "C3": (16.5, 14.6, 90),
    "D4": (17.5, 3.2, 90), "D5": (20.1, 3.2, 90), "D6": (22.7, 3.2, 90), "D7": (25.3, 3.2, 90),
    "D8": (27.9, 3.2, 90), "D9": (30.5, 3.2, 90),
    "R6": (17.5, 7.8, 90), "R7": (20.1, 7.8, 90), "R8": (22.7, 7.8, 90), "R9": (25.3, 7.8, 90),
    "R10": (27.9, 7.8, 90), "R11": (30.5, 7.8, 90),
    "C1": (39.8, 8.6, 0),
    # TOP RIGHT, next to the Super Mini: CR2032 standing vertical, DS1307 + crystal + pull-ups, FLOW LED
    "BT1": (84.2, 11.2, 90),
    "U2": (91.8, 5.0, 90),
    "Y1": (91.0, 15.0, 0),
    "D3": (97.0, 3.2, 90), "R4": (97.0, 7.8, 90),
    "R1": (94.6, 13.6, 90), "R2": (97.0, 13.6, 90), "C2": (94.6, 17.6, 90),
}
OFF_EDGE_OK = {"J1", "J2", "J3", "J4", "U1"}      # pluggable headers and the USB end overhang the edge by design

EDGE_KEEP = 0.5                                     # no tracks or vias this close to the board edge
NO_TRACK = [(0, 0, 99.0, EDGE_KEEP), (0, 48.0 - EDGE_KEEP, 99.0, 48.0), (0, 0, EDGE_KEEP, 48.0), (99.0 - EDGE_KEEP, 0, 99.0, 48.0)]
NO_POUR = [(0.4, 20.6, 45.0, 47.6),                 # 230 V corner
           (80.0, 24.6, 98.6, 30.0)]                # Super Mini antenna end

# Pre-routed, locked tracks: net, [points: (ref, pad) or (x, y)], width mm. The 230 V contacts run straight
# down to their header.
PREROUTE = [
    ("/PUMP_NO", [("K1", "3"), ("J2", "1")], 1.5),
    ("/PUMP_COM", [("K1", "1"), (CX["K1"], 41.0), ("J2", "2")], 1.5),
    ("/DOSER_NO", [("K2", "3"), ("J3", "1")], 1.5),
    ("/DOSER_COM", [("K2", "1"), (CX["K2"], 41.0), ("J3", "2")], 1.5),
    # K1/K2 coil pins sit 3.6 mm from their own COM (the SRD's pinout): short stubs lead them out to
    # y 19, > 5 mm from 230 V copper, ending on a via the router can reach
    ("/COIL_PUMP", [("K1", "2"), ("@x", 19.0)], 0.5, "via"), ("+5V", [("K1", "5"), ("@x", 19.0)], 1.0, "via"),
    ("/COIL_DOSER", [("K2", "2"), ("@x", 19.0)], 0.5, "via"), ("+5V", [("K2", "5"), ("@x", 19.0)], 1.0, "via"),
]

# Net classes: name -> (track width mm, clearance mm, nets). Freerouting gets these clearances;
# for DRC, WM-MiniC3.kicad_dru adds the 230 V rules (5 mm to everything, 3.5 mm inside K1/K2, 2.4 mm pump-doser).
MAINS = ["/PUMP_COM", "/PUMP_NO", "/DOSER_COM", "/DOSER_NO", "unconnected-(K1-Pad4)", "unconnected-(K2-Pad4)"]
NETCLASSES = {
    "Supply5V": (1.0, 0.2, ["+5V"]),        # 1 mm: USB 5 V from the Super Mini to all six relay coils
    "Power": (0.5, 0.2, ["GND", "/COIL_PUMP", "/COIL_DOSER", "/COIL_V1", "/COIL_V2", "/COIL_V3", "/COIL_V4"]),
    "Valve": (0.8, 0.4, ["/VALVE_COM", "/V1_NO", "/V2_NO", "/V3_NO", "/V4_NO"]),
    "Mains": (1.5, 5.0, MAINS),
}

def silk(T):
    """Silkscreen: connector pin names (front, inside each header's outline), connector titles (back,
    above the headers), LED names, warnings, back-side title."""
    for ref, names, title in (("J2", ("NO", "COM"), "PUMP 230V~"), ("J3", ("NO", "COM"), "DOSER 230V~"),
                              ("J4", ("COM", "V1", "V2", "V3", "V4"), "VALVES 12/24VAC"),
                              ("J1", ("5V", "GND", "SIG"), "FLOW")):
        xs = []
        for i, name in enumerate(names, 1):
            x, y = T.pad_xy(ref, str(i))
            xs.append(x)
            T.text(name, x, 43.35, 0.75, bold=True)
        T.text(title, (xs[0] + xs[-1]) / 2, 42.9, 0.9, layer="B", bold=True)
    for ref, name in (("D4", "D"), ("D5", "P"), ("D6", "V1"), ("D7", "V2"), ("D8", "V3"), ("D9", "V4"), ("D3", "F")):
        x, y = T.pad_xy(ref, "1")
        T.text(name, x, 10.6, 0.8, bold=True)
    T.text("230V~ KEEP CLEAR", 24.1, 21.25, 0.9, bold=True)
    T.text("USB 5V 2A", 89.9, 23.3, 0.8, bold=True)
    T.text("WM-Mini C3 rev A - Agri Sensors and Controls - Oct 2026", 63.0, 21.25, 0.75, layer="B", bold=True)

ASSEMBLY_VALUE = {
    "U1": "ESP32-C3", "U2": "DS1307", "U3": "ULN2003", "Y1": "32K", "BT1": "CR2032",
    "K1": "PUMP", "K2": "DOSER", "K3": "V2", "K4": "V3", "K5": "V1", "K6": "V4",
    "C1": "1000U", "C2": "100N", "C3": "100N", "R1": "4K7", "R2": "4K7", "R3": "10K",
    "D1": "B5819W", "D2": "TVS5V",
}
LABEL_INSIDE = {"K1", "K2", "K3", "K4", "K5", "K6", "BT1"}
MARKS = [("U2", "1", "1")]          # U3 carries its own pin-1 mark

def netname(n):
    # power nets and KiCad's own names for short wired-only links (LED anode to its resistor) stay as they are
    return n if n in POWER_NETS or n.startswith("Net-(") else "/" + n
