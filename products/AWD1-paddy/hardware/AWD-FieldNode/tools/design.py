"""AWD field node v0.1 - single source of truth for parts, nets and placement.

Used by gen_sch.py (schematic, system python) and gen_pcb.py (board, KiCad python).
Net names follow KiCad's convention for local labels on the root sheet ("/NAME");
power nets (GND, +3V3) are global.
"""

PROJECT = "AWD-FieldNode"
POWER_NETS = {"GND", "+3V3"}

# ---- strip geometry (mm, board coordinates) ---------------------------------
HEAD = (97.5, 40.0, 137.5, 100.0)          # x0, y0, x1, y1  (40 x 60 mm)
STRIP_X0, STRIP_X1 = 112.5, 122.5          # 10 mm wide
STRIP_Y0, STRIP_Y1 = 100.0, 400.0          # 300 mm long
STRIP_CX = 117.5
PAD_W, PAD_H, PAD_PITCH = 8.0, 18.0, 20.0
P1_CENTER_Y = 135.0                        # P1 spans +7..+5 cm, P12 spans -15..-17 cm
REF_CENTER_Y, REF_H = 116.0, 12.0
GND_PAD_CENTER_Y, GND_PAD_H = 384.0, 20.0
SOIL_Y = 196.0                             # 0 cm mark
LANE_PITCH = 0.6                           # In2.Cu trace pitch inside the strip
RS_PITCH = 1.6                             # series-resistor row pitch in the head
RS_Y = 92.0
FAN_START_Y = 100.5
VIA_Y = 93.5

def pad_center_y(i):            # i = 1..12
    return P1_CENTER_Y + PAD_PITCH * (i - 1)

def level_cm(y):                # board y -> cm relative to soil surface
    return (SOIL_Y - y) / 10.0

# ---- parts --------------------------------------------------------------------
# (ref, lib_id, value, footprint, sch_xy, {pin (number or name): net})
# Unlisted pins are marked no-connect in the schematic.
P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

R0603 = "Resistor_SMD:R_0603_1608Metric"
R0402 = "Resistor_SMD:R_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"

# Power: LiFePO4 14500 straight onto the 3V3 rail (3.0-3.4 V plateau), P-FET reverse protection
part("J3", "Connector_Generic:Conn_01x02", "LiFePO4 14500", "Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal",
     (30.48, 50.8), {"1": "VBAT_RAW", "2": "GND"})
part("Q1", "Transistor_FET:AO3401A", "AO3401A", SOT23, (55.88, 50.8), {"G": "Q1_G", "S": "+3V3", "D": "VBAT_RAW"})
part("R1", "Device:R", "100k", R0603, (71.12, 50.8), {"1": "Q1_G", "2": "GND"})
part("C1", "Device:C", "47u", C1206, (88.9, 50.8), {"1": "+3V3", "2": "GND"})
part("C2", "Device:C", "10u", C0805, (101.6, 50.8), {"1": "+3V3", "2": "GND"})
part("C3", "Device:C", "100n", C0603, (114.3, 50.8), {"1": "+3V3", "2": "GND"})
part("C4", "Device:C", "100n", C0603, (127.0, 50.8), {"1": "+3V3", "2": "GND"})
part("C5", "Device:C", "10u", C0805, (139.7, 50.8), {"1": "+3V3", "2": "GND"})

# MCU
esp = {"3V3": "+3V3", "GND": "GND", "EN": "EN", "IO0": "BOOT",
       "IO14": "T14_SPARE", "IO15": "VBAT_ADC", "IO16": "VBAT_EN", "IO17": "LORA_DIO1", "IO18": "LORA_BUSY",
       "USB_D-": "USB_DN", "USB_D+": "USB_DP", "IO21": "LORA_RST", "IO38": "LED",
       "IO39": "LORA_NSS", "IO40": "LORA_SCK", "IO41": "LORA_MOSI", "IO42": "LORA_MISO",
       "TXD0": "TXD0", "RXD0": "RXD0"}
for i in range(1, 13):
    esp["IO%d" % i] = "T%d" % i            # touch channel T1..T12 -> pads P1..P12
esp["IO13"] = "T13"                         # touch channel T13 -> REF pad
part("U1", "RF_Module:ESP32-S3-MINI-1", "ESP32-S3-MINI-1-N8", "RF_Module:ESP32-S2-MINI-1", (114.3, 144.78), esp)
part("R2", "Device:R", "10k", R0603, (40.64, 101.6), {"1": "+3V3", "2": "EN"})
part("C6", "Device:C", "1u", C0603, (40.64, 119.38), {"1": "EN", "2": "GND"})
part("R3", "Device:R", "10k", R0603, (58.42, 101.6), {"1": "+3V3", "2": "BOOT"})
part("SW1", "Switch:SW_Push", "PAIR/BOOT", "Button_Switch_SMD:SW_SPST_TL3342", (58.42, 121.92), {"1": "BOOT", "2": "GND"})

# Battery sense: 1M/1M divider, low side switched so it draws nothing in sleep
part("R4", "Device:R", "1M", R0603, (182.88, 124.46), {"1": "+3V3", "2": "VBAT_ADC"})
part("R5", "Device:R", "1M", R0603, (182.88, 144.78), {"1": "VBAT_ADC", "2": "DIV_LO"})
part("C7", "Device:C", "100n", C0603, (167.64, 134.62), {"1": "VBAT_ADC", "2": "GND"})
part("Q2", "Transistor_FET:2N7002", "2N7002", SOT23, (200.66, 160.02), {"D": "DIV_LO", "S": "GND", "G": "VBAT_EN"})
part("R6", "Device:R", "100k", R0603, (220.98, 162.56), {"1": "VBAT_EN", "2": "GND"})

# Status LED
part("R7", "Device:R", "1k", R0603, (182.88, 182.88), {"1": "LED", "2": "LED_A"})
part("D1", "Device:LED", "LED", "LED_SMD:LED_0603_1608Metric", (200.66, 190.5), {"2": "LED_A", "1": "GND"})

# LoRa: same ISC-SX1262-B module + pinout as WPC (Ra-01 footprint, BUSY on pad 10)
part("U2", "AWD:ISC-SX1262-B", "ISC-SX1262-B", "RF_Module:Ai-Thinker-Ra-01-LoRa", (243.84, 76.2),
     {"VDD": "+3V3", "GND": "GND", "MOSI": "LORA_MOSI", "MISO": "LORA_MISO", "SCK": "LORA_SCK",
      "~{NSS}": "LORA_NSS", "~{RESET}": "LORA_RST", "DIO1": "LORA_DIO1", "DIO4/BUSY": "LORA_BUSY", "ANT": "RF_ANT"})
part("J1", "Connector:Conn_Coaxial", "SMA 866MHz", "Connector_Coaxial:SMA_Amphenol_132289_EdgeMount",
     (299.72, 66.04), {"1": "RF_ANT", "2": "GND"})

# Programming / jig pads (native USB + UART log), 1.27 mm so a pogo jig or header fits
part("J2", "Connector_Generic:Conn_01x08", "PROG", "Connector_PinHeader_1.27mm:PinHeader_1x08_P1.27mm_Vertical",
     (276.86, 132.08), {"1": "+3V3", "2": "GND", "3": "EN", "4": "BOOT", "5": "USB_DN", "6": "USB_DP",
                        "7": "TXD0", "8": "RXD0"})
part("TP1", "Connector:TestPoint", "T14", "TestPoint:TestPoint_Pad_D1.5mm", (304.8, 132.08), {"1": "T14_SPARE"})

# Touch series resistors (Espressif recommends ~510R close to the chip) + sensing pads
for i in range(1, 14):
    sense = "P%d" % i if i <= 12 else "REF"
    part("RS%d" % i, "Device:R", "510", R0402, (35.56 + 12.7 * (i - 1), 213.36), {"1": "T%d" % i, "2": sense})
for i in range(1, 13):
    part("E%d" % i, "AWD:SensePad", "P%d" % i, "AWD:SensePad_8x18mm", (35.56 + 12.7 * (i - 1), 248.92), {"1": "P%d" % i})
part("E13", "AWD:SensePad", "REF", "AWD:SensePad_8x12mm", (35.56 + 12.7 * 12, 248.92), {"1": "REF"})
part("E14", "AWD:SensePad", "GND return", "AWD:ReturnPad_8x20mm", (35.56 + 12.7 * 13, 248.92), {"1": "GND"})

PWR_FLAGS = [("+3V3", (165.1, 50.8)), ("GND", (177.8, 50.8)), ("VBAT_RAW", (190.5, 50.8))]

def netname(n):
    return n if n in POWER_NETS else "/" + n
