"""ASC Mini-C3 rev B: the single source of truth for parts, nets and placement.

The low-cost Mini (2026-10-10), shared with FG1 (FlowGuard). The ESP32-C3 Super Mini plugs
into two 8-pin sockets and its USB-C powers the board (a 5 V / 1 A USB-C charger in the field).
On the board: a YF-S401 / YF-S201 flow input, a 5 V DC pump output (MOSFET), one 230 V relay
(HF46F, 5 A) for a pump contactor coil, a DS1307 RTC with a tabbed CR2032, an I2C terminal for
the ASC-AHT20 sensor board (T/RH outside the box), a 5-pole AUX terminal (1-Wire/DHT + 2 ADC)
and four LEDs (PUMP, FLOW, STATUS, RELAY) that show through the clear lid.

Board: 48 x 44 mm. A 100 x 100 panel holds 4 Minis (96 x 88) plus 4 ASC-AHT20 boards (24 x 12).
Mounted on 3 nylon snap standoffs taped to the box floor, so it fits any box with a flat floor (IP-65-26 chosen).

Used by gen_sch.py (schematic + netlist, system python3) and gen_pcb.py / route.py (KiCad's python).
Net names follow KiCad's convention for local labels on the root sheet ("/NAME").
"""

PROJECT = "ASC-MiniC3"
TITLE = "ASC Student Kit - Mini-C3 rev B (low cost, FG1 common)"
DATE, REV, PAPER = "2026-10-10", "B", "A3"
COMMENT = "48 x 44 mm; 4 Minis + 4 AHT20 sticks per 100 x 100 panel; 3 snap-standoff holes (any box)"
POWER_NETS = {"GND", "+3V3", "+5V"}

# ---- footprints -------------------------------------------------------------------
# 0805 passives: hand-solderable, and all JLC basic parts
R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
LED0805 = "LED_SMD:LED_0805_2012Metric"
SOD123 = "Diode_SMD:D_SOD-123"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
# 3.81 mm screw terminals, the same family as the WPC Master and PumpNode (KF128-3.81 equivalent)
TB = "TerminalBlock_MetzConnect:TerminalBlock_MetzConnect_Type086_RT0340%dHBLC_1x0%d_P3.81mm_Horizontal"
# 5.08 mm terminal for the 230 V relay contacts (KF301-5.08 equivalent)
TB_MAINS = "TerminalBlock_MetzConnect:TerminalBlock_MetzConnect_Type101_RT01602HBWC_1x02_P5.08mm_Horizontal"
XH3 = "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical"    # mates with the YF-S401's JST-XH plug

# ---- parts --------------------------------------------------------------------------
# part(ref, lib_id, value, footprint, (x, y) on the A3 sheet in mm, {pin number or name: net})
# Unlisted pins are marked no-connect in the schematic.
P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

NOTES = []
def note(text, x, y):
    NOTES.append((text, x, y))

# ---------------------------------------------------------------- MCU: ESP32-C3 Super Mini
X, Y = 76.2, 88.9
note("MCU: ESP32-C3 Super Mini, plugged into 2 x 8-pin sockets.", 25.4, 40.64)
note("Its USB-C powers the board (5 V / 1 A charger in the field): the 5V pin", 25.4, 44.45)
note("(USB VBUS) feeds the pump, the relay coil, the flow sensor and the RTC;", 25.4, 48.26)
note("the 3V3 pin (on-module LDO) feeds the sensors and pull-ups.", 25.4, 52.07)
note("Left free: GPIO2 and GPIO8/9 (strapping; GPIO8 = module LED, GPIO9 = BOOT), GPIO21.", 25.4, 55.88)
part("U1", "ASC:ESP32-C3_SuperMini", "ESP32-C3 Super Mini", "ASC:ESP32-C3_SuperMini_Socket", (X, Y),
     {"5V": "+5V", "GND": "GND", "3V3": "+3V3",
      "IO0": "AUX_A0", "IO1": "AUX_A1", "IO3": "PUMP", "IO4": "AUX_D",
      "IO5": "FLOW", "IO6": "SDA", "IO7": "SCL", "IO10": "LED_STATUS", "IO20": "RELAY"})

# ---------------------------------------------------------------- RTC DS1307 + I2C terminal
X, Y = 195.58, 88.9
note("RTC: DS1307 at 0x68, on 5 V (it needs 4.5-5.5 V).", 154.94, 40.64)
note("I2C pull-ups go to 3V3: the DS1307 reads 3.3 V as high (VIH 2.2 V).", 154.94, 44.45)
note("Backup: CR2032 with solder tabs, standing vertical (no holder).", 154.94, 48.26)
note("Crystal 32.768 kHz, 12.5 pF load.", 154.94, 52.07)
part("U2", "Timer_RTC:DS1307Z+", "DS1307Z+", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", (X, Y),
     {"VCC": "+5V", "GND": "GND", "X1": "XT1", "X2": "XT2", "VBAT": "VBAT", "SDA": "SDA", "SCL": "SCL"})
part("C2", "Device:C", "100n", C0805, (X + 25.4, Y - 15.24), {"1": "+5V", "2": "GND"})
part("Y1", "Device:Crystal", "32.768k 12.5pF", "Crystal:Crystal_C26-LF_D2.1mm_L6.5mm_Horizontal", (X - 30.48, Y + 3.81),
     {"1": "XT1", "2": "XT2"})
part("BT1", "Device:Battery_Cell", "CR2032 tabbed", "ASC:Battery_CR2032_Vertical_3pin", (X - 30.48, Y + 25.4),
     {"1": "VBAT", "2": "GND"})
part("R8", "Device:R", "4.7k", R0805, (X + 38.1, Y + 2.54), {"1": "+3V3", "2": "SDA"})
part("R9", "Device:R", "4.7k", R0805, (X + 50.8, Y + 2.54), {"1": "+3V3", "2": "SCL"})
note("I2C terminal: 3V3 GND SCL SDA, the pin order of the ASC-AHT20", 292.1, 40.64)
note("board and of most AHT20 / BMP280 / BH1750 modules (VIN GND SCL SDA).", 292.1, 44.45)
part("J5", "Connector:Screw_Terminal_01x04", "I2C", TB % (4, 4), (317.5, 88.9),
     {"1": "+3V3", "2": "GND", "3": "SCL", "4": "SDA"})

# ---------------------------------------------------------------- AUX terminal
X, Y = 45.72, 190.5
note("AUX: D = GPIO4 with a 10k pull-up (DHT11/DHT22 or a DS18B20 probe),", 25.4, 157.48)
note("A0/A1 = GPIO0/1 on ADC1 (soil-moisture probe, LDR, pot, switch).", 25.4, 161.29)
part("J3", "Connector:Screw_Terminal_01x05", "AUX", TB % (5, 5), (X, Y),
     {"1": "+3V3", "2": "AUX_D", "3": "AUX_A0", "4": "AUX_A1", "5": "GND"})
part("R7", "Device:R", "10k", R0805, (X + 25.4, Y - 7.62), {"1": "+3V3", "2": "AUX_D"})

# ---------------------------------------------------------------- flow sensor YF-S401
X, Y = 132.08, 182.88
note("Flow: YF-S401 on 5 V (needs 5-24 V), 5880 pulses per litre.", 116.84, 157.48)
note("Its output can swing to 5 V, so Schottky D2 only pulls GPIO5 low", 116.84, 161.29)
note("and R6 pulls it up to 3V3. The FLOW LED sinks into the sensor", 116.84, 165.1)
note("output, so it blinks with every pulse (no GPIO needed).", 116.84, 168.91)
part("J4", "Connector_Generic:Conn_01x03", "FLOW", XH3, (X, Y), {"1": "GND", "2": "+5V", "3": "FLOW_EXT"})
part("D2", "Device:D_Schottky", "B5819W", SOD123, (X + 25.4, Y + 2.54), {"1": "FLOW_EXT", "2": "FLOW"})
part("R6", "Device:R", "10k", R0805, (X + 45.72, Y - 2.54), {"1": "+3V3", "2": "FLOW"})
part("R5", "Device:R", "470", R0805, (X + 12.7, Y + 30.48), {"1": "+3V3", "2": "LED_FLOW_A"})
part("D4", "Device:LED", "FLOW yellow", LED0805, (X + 35.56, Y + 38.1), {"1": "FLOW_EXT", "2": "LED_FLOW_A"})

# ---------------------------------------------------------------- pump driver
X, Y = 274.32, 190.5
note("Pump: 5 V DC submersible (about 0.2-0.3 A). Low-side AO3400A;", 228.6, 157.48)
note("R2 holds it off through reset; D1 is the flyback diode.", 228.6, 161.29)
note("PUMP LED sits across the switch side: lit while the pump is driven.", 228.6, 165.1)
note("C1 rides out the start-up surge of the pump and the relay on USB 5 V.", 228.6, 168.91)
part("R1", "Device:R", "100", R0805, (X - 33.02, Y + 7.62), {"1": "PUMP", "2": "PUMP_G"})
part("R2", "Device:R", "100k", R0805, (X - 33.02, Y + 25.4), {"1": "PUMP", "2": "GND"})
part("Q1", "Transistor_FET:AO3400A", "AO3400A", SOT23, (X - 12.7, Y + 15.24), {"G": "PUMP_G", "S": "GND", "D": "PUMP_N"})
part("D1", "Device:D_Schottky", "B5819W", SOD123, (X + 7.62, Y - 2.54), {"1": "+5V", "2": "PUMP_N"})
part("J1", "Connector:Screw_Terminal_01x02", "PUMP", TB % (2, 2), (X + 33.02, Y), {"1": "+5V", "2": "PUMP_N"})
part("C1", "Device:C_Polarized", "220u 10V", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", (X - 45.72, Y - 7.62),
     {"1": "+5V", "2": "GND"})
part("R3", "Device:R", "1k", R0805, (X + 15.24, Y + 30.48), {"1": "+5V", "2": "LED_PUMP_A"})
part("D3", "Device:LED", "PUMP red", LED0805, (X + 35.56, Y + 38.1), {"1": "PUMP_N", "2": "LED_PUMP_A"})

# ---------------------------------------------------------------- relay (FG1 pump contactor)
X, Y = 330.2, 182.88
note("Relay (FG1): HF46F 5 V coil, 1 Form A, 5 A 250 VAC, for a pump contactor coil.", 309.88, 157.48)
note("GPIO20 (UART0 RX, an input at boot, so the relay stays off) drives AO3400A Q2;", 309.88, 161.29)
note("R11 holds it off through reset. RELAY LED across the coil. Contacts on a 5.08 mm", 309.88, 165.1)
note("terminal; 230 V copper keeps >= 5 mm from everything else (.kicad_dru rule).", 309.88, 168.91)
part("R10", "Device:R", "100", R0805, (X - 7.62, Y + 15.24), {"1": "RELAY", "2": "RELAY_G"})
part("R11", "Device:R", "100k", R0805, (X - 7.62, Y + 33.02), {"1": "RELAY", "2": "GND"})
part("Q2", "Transistor_FET:AO3400A", "AO3400A", SOT23, (X + 10.16, Y + 22.86), {"G": "RELAY_G", "S": "GND", "D": "RELAY_COIL"})
part("D6", "Device:D_Schottky", "B5819W", SOD123, (X + 20.32, Y + 5.08), {"1": "+5V", "2": "RELAY_COIL"})
part("K1", "ASC:HF46F", "HF46F/005-HS1", "ASC:Relay_SPST_Hongfa_HF46F", (X + 43.18, Y + 7.62),
     {"1": "+5V", "2": "RELAY_COIL", "3": "RELAY_NO", "4": "RELAY_COM"})
part("J6", "Connector:Screw_Terminal_01x02", "RELAY 230V", TB_MAINS, (X + 66.04, Y + 7.62),
     {"1": "RELAY_COM", "2": "RELAY_NO"})
part("R12", "Device:R", "1k", R0805, (X + 33.02, Y + 30.48), {"1": "+5V", "2": "LED_RLY_A"})
part("D7", "Device:LED", "RELAY blue", LED0805, (X + 50.8, Y + 38.1), {"1": "RELAY_COIL", "2": "LED_RLY_A"})

# ---------------------------------------------------------------- status LED
X, Y = 132.08, 254.0
note("STATUS LED on GPIO10 (active high), for the firmware.", 116.84, 241.3)
part("R4", "Device:R", "1k", R0805, (X + 12.7, Y + 2.54), {"1": "LED_STATUS", "2": "LED_STATUS_A"})
part("D5", "Device:LED", "STATUS green", LED0805, (X + 35.56, Y + 10.16), {"1": "GND", "2": "LED_STATUS_A"})

# ---------------------------------------------------------------- mechanical
note("Mounting: 3 x 4.2 mm holes for 5 mm nylon reverse-mount snap standoffs, bases taped to the box floor (any box).", 228.6, 241.3)
for i, xy in enumerate([(233.68, 254.0), (248.92, 254.0), (264.16, 254.0)], 1):
    part("H%d" % i, "Mechanical:MountingHole", "SNAP 4.2", "ASC:Snap_Standoff_4.2mm", xy, {})

PWR_FLAGS = [("GND", (322.58, 254.0)), ("VBAT", (342.9, 254.0))]

# ---- board geometry, for gen_pcb.py ------------------------------------------------------------
BOARD_W, BOARD_H, CORNER_R = 48.0, 44.0, 0.0   # square corners for the V-cut panel
HOLES = [(44.4, 12.6), (3.7, 26.8), (32.9, 40.0)]   # snap standoffs, a triangle around the centre of mass
SLOTS = []                                  # internal cut-outs (x0, y0, x1, y1)

# Placement: ref -> (x, y, rotation). (x, y) is where the centre of the part's courtyard goes,
# board coordinates from the top-left corner, y down. Rotation is KiCad's (counter-clockwise).
# The 3.81 mm terminals' wire entry is at the footprint's -y side: 0 faces the top edge,
# 180 the bottom edge.
PLACE = {
    "U1": (10.0, 10.78, 0),          # USB-C end flush with the top edge
    # under the module (it sits ~9 mm up on its sockets): the RTC
    "U2": (6.6, 9.0, 90),
    "Y1": (11.45, 9.0, 0),            # crystal lying down
    "C2": (14.9, 5.2, 90),
    "R8": (14.9, 8.9, 90),
    "R9": (14.9, 12.6, 90),
    # top edge: I2C terminal, FLOW socket; coin cell and a hole below them
    "J5": (28.1, 4.25, 0),
    "J4": (42.3, 3.5, 0),
    "BT1": (30.1, 13.0, 0),
    # LED row, seen through the lid: PUMP, FLOW, STATUS, RELAY, each resistor right above it
    "R3": (28.5, 18.0, 0), "D3": (28.5, 21.0, 0),
    "R5": (32.6, 18.0, 0), "D4": (32.6, 21.0, 0),
    "R4": (36.7, 18.0, 0), "D5": (36.7, 21.0, 0),
    "R12": (40.8, 18.0, 0), "D7": (40.8, 21.0, 0),
    # 230 V corner: relay contacts and the 5.08 mm terminal, bottom right
    "K1": (37.15, 30.5, 0),
    "J6": (42.23, 39.44, 180),
    # relay driver, between the module and the relay coil
    "Q2": (23.6, 24.6, 0),
    "D6": (24.6, 30.0, 90),
    "R10": (21.3, 28.2, 90),
    "R11": (21.3, 31.9, 90),
    # pump driver and the flow / AUX parts, left of the relay
    "C1": (16.5, 26.0, 0),
    "Q1": (10.0, 25.0, 0),
    "D1": (11.0, 30.6, 90),
    "R1": (8.5, 30.6, 90),
    "R2": (13.6, 31.6, 90),
    "R6": (16.0, 31.6, 90),
    "D2": (4.0, 32.0, 0),
    "R7": (18.4, 31.6, 90),
    # bottom edge: AUX and PUMP terminals, wire entry facing down
    "J3": (10.4, 39.75, 180),
    "J1": (24.9, 39.75, 180),
}

# No copper pour: the Super Mini's antenna end, and the whole 230 V corner
NO_POUR = [(1.0, 16.5, 19.0, 23.0),
           (32.8, 26.6, 48.0, 44.0)]

# Tracks drawn by gen_pcb.py and locked before autorouting: net, (ref, pad), (ref, pad), width mm.
# The 230 V contacts run straight down to their terminal on the top layer.
PREROUTE = [("/RELAY_NO", ("K1", "3"), ("J6", "2"), 1.5),
            ("/RELAY_COM", ("K1", "4"), ("J6", "1"), 1.5)]
NO_TRACK = []                               # (x0, y0, x1, y1) areas where no track or via may go

# Net classes for routing: name -> (track width mm, clearance mm, nets). The mains clearance
# applies to Freerouting; for DRC, ASC-MiniC3.kicad_dru keeps mains 5 mm from everything else.
NETCLASSES = {
    "Power": (0.5, 0.2, ["+5V", "GND", "/PUMP_N", "/RELAY_COIL"]),
    "Mains": (1.5, 5.0, ["/RELAY_COM", "/RELAY_NO"]),
}

def silk(T):
    """Silkscreen labels; T gives text(), pad_xy(), box_mm()."""
    for ref, names, title in (("J3", {"1": "3V3", "2": "D", "3": "A0", "4": "A1", "5": "GND"}, "AUX"),
                              ("J1", {"1": "P+", "2": "P-"}, "PUMP")):
        b = T.box_mm(ref)
        for num, name in names.items():
            x, y = T.pad_xy(ref, num)
            T.text(name, x, b[1] - 0.75)
    b = T.box_mm("J5")
    for num, name in {"1": "3V3", "2": "GND", "3": "SCL", "4": "SDA"}.items():
        x, y = T.pad_xy("J5", num)
        T.text(name, x, b[3] + 0.85)
    b = T.box_mm("J4")
    for num, name in {"1": "G", "2": "5V", "3": "S"}.items():
        x, y = T.pad_xy("J4", num)
        T.text(name, x, b[3] + 0.6)
    T.text("FLOW", (b[0] + b[2]) / 2, b[3] + 1.9, 0.8, bold=True)
    for ref, name in (("D3", "PUMP"), ("D4", "FLOW"), ("D5", "STAT"), ("D7", "RLY")):
        b = T.box_mm(ref)
        T.text(name, (b[0] + b[2]) / 2, b[3] + 0.75, 0.8, bold=True)
    for num, name in {"1": "COM", "2": "NO"}.items():
        x, y = T.pad_xy("J6", num)
        T.text(name, x, 30.85, 0.8)
    T.text("230V~ 5A", 37.0, 25.3, 0.9, bold=True)
    T.text("ASC Mini-C3 rev B", 34.0, 17.8, 1.2, layer="B", bold=True)
    T.text("Agri Sensors and Controls", 34.0, 19.7, 0.9, layer="B")
    T.text("RELAY 230 V: DISCONNECT", 34.0, 21.4, 0.8, layer="B")
    T.text("MAINS BEFORE OPENING", 34.0, 22.8, 0.8, layer="B")

def netname(n):
    return n if n in POWER_NETS else "/" + n
