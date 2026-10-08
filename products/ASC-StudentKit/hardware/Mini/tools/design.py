"""ASC Mini rev A: the single source of truth for parts and nets.

Used by gen_sch.py (schematic + netlist.json, system python3) and, later, by the
board generator (KiCad python). Pin assignments follow ../../pinmap.json and the
kit architecture (docs/StudentKit_Architecture_v0.1.md §2-§4, §8); check_design.py
checks them against pinmap.json.

Board: 97.4 x 47.0 mm half board, 3 x M3 on the box's upper bosses (architecture §8).
Net names follow KiCad's convention for local labels on the root sheet ("/NAME");
power nets are global.
"""

PROJECT = "ASC-Mini"
POWER_NETS = {"GND", "+3V3", "+5V"}

# ---- footprints -------------------------------------------------------------------
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0805 = "LED_SMD:LED_0805_2012Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOD123 = "Diode_SMD:D_SOD-123"
DSMA = "Diode_SMD:D_SMA"
PTC1206 = "Fuse:Fuse_1206_3216Metric"
XH4 = "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical"
GROVE = "Connector_JST:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical"   # footprint stand-in for HY2.0-4P (Grove); swap at layout
TERM2 = "Connector_Phoenix_MSTB:PhoenixContact_MSTBA_2,5_2-G-5,08_1x02_P5.08mm_Horizontal"
BTN = "Button_Switch_SMD:SW_SPST_TL3342"

# ---- parts --------------------------------------------------------------------------
# part(ref, lib_id, value, footprint, (x, y) on the A3 sheet in mm, {pin number or name: net})
# Unlisted pins are marked no-connect in the schematic.
P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

NOTES = []
def note(text, x, y):
    NOTES.append((text, x, y))

# ---------------------------------------------------------------- 12 V input -> 5 V
X, Y = 25.4, 55.88
note("12 V IN (9-24 V): reverse-polarity diode D1, TVS D2, then the TPS54202 buck set to 5.3 V; D3 drops it to ~5.0 V on the +5V rail.", 20.32, 33.02)
part("J1", "Connector_Generic:Conn_01x02", "12V IN", TERM2, (X, Y), {"1": "VIN_RAW", "2": "GND"}, silk="12V IN  + -")
part("D1", "Diode:SS34", "SS34", DSMA, (X + 17.78, Y), {"A": "VIN_RAW", "K": "VIN"})
part("D2", "Diode:SMAJ26A", "SMAJ26A", DSMA, (X + 38.1, Y), {"1": "VIN", "2": "GND"})
part("C1", "Device:C_Polarized", "47u 35V", "Capacitor_SMD:CP_Elec_6.3x5.4", (X + 53.34, Y), {"1": "VIN", "2": "GND"})
part("C2", "Device:C", "10u 50V", C1206, (X + 63.5, Y), {"1": "VIN", "2": "GND"})
part("C3", "Device:C", "100n 50V", C0603, (X + 73.66, Y), {"1": "VIN", "2": "GND"})
# EN floats: the TPS54202's internal pull-up enables it, so EN is left unconnected on purpose.
part("U2", "Regulator_Switching:TPS54202DDC", "TPS54202DDC", "Package_TO_SOT_SMD:SOT-23-6", (X + 101.6, Y),
     {"VIN": "VIN", "GND": "GND", "BOOT": "BUCK_BOOT", "SW": "BUCK_SW", "FB": "BUCK_FB"})
part("C4", "Device:C", "100n", C0603, (X + 124.46, Y - 5.08), {"1": "BUCK_BOOT", "2": "BUCK_SW"})
part("L1", "Device:L", "10u 3A", "Inductor_SMD:L_6.3x6.3_H3", (X + 137.16, Y), {"1": "BUCK_SW", "2": "+5V_BUCK"})
part("R1", "Device:R", "78.7k 1%", R0603, (X + 149.86, Y), {"1": "+5V_BUCK", "2": "BUCK_FB"})
part("R2", "Device:R", "10k 1%", R0603, (X + 162.56, Y), {"1": "BUCK_FB", "2": "GND"})
part("C5", "Device:C", "22u 10V", C1206, (X + 175.26, Y), {"1": "+5V_BUCK", "2": "GND"})
part("C6", "Device:C", "22u 10V", C1206, (X + 187.96, Y), {"1": "+5V_BUCK", "2": "GND"})
part("D3", "Diode:SS34", "SS34", DSMA, (X + 205.74, Y), {"A": "+5V_BUCK", "K": "+5V"})

# VIN_SENSE: 100k/10k -> 24 V reads 2.18 V on GPIO10 (ADC1_CH9); 0 V means running from USB
part("R5", "Device:R", "100k 1%", R0603, (X + 228.6, Y), {"1": "VIN", "2": "VIN_SENSE"})
part("R6", "Device:R", "10k 1%", R0603, (X + 241.3, Y), {"1": "VIN_SENSE", "2": "GND"})
part("C7", "Device:C", "100n", C0603, (X + 254.0, Y), {"1": "VIN_SENSE", "2": "GND"})

# ---------------------------------------------------------------- USB-C -> 5 V, D+/D-
X, Y = 30.48, 109.22
note("USB-C: flashing and the serial console (native USB on GPIO19/20), and 5 V for desk use. F1 + D4 feed the +5V rail; CC pull-downs ask for 5 V.", 20.32, 81.28)
part("J2", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
     (X, Y), {"VBUS": "VBUS", "GND": "GND", "SHIELD": "GND", "CC1": "USB_CC1", "CC2": "USB_CC2",
              "A7": "USB_DN", "B7": "USB_DN", "A6": "USB_DP", "B6": "USB_DP"})
part("R3", "Device:R", "5.1k", R0603, (X + 33.02, Y - 7.62), {"1": "USB_CC1", "2": "GND"})
part("R4", "Device:R", "5.1k", R0603, (X + 45.72, Y - 7.62), {"1": "USB_CC2", "2": "GND"})
part("F1", "Device:Polyfuse_Small", "0.5A hold", PTC1206, (X + 58.42, Y - 7.62), {"1": "VBUS", "2": "VBUS_F"})
part("D4", "Diode:SS34", "SS34", DSMA, (X + 73.66, Y - 7.62), {"A": "VBUS_F", "K": "+5V"})
part("U3", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", (X + 101.6, Y + 2.54),
     {"I/O1": "USB_DN", "I/O2": "USB_DP", "VBUS": "VBUS", "GND": "GND"})   # pins 1/6 and 3/4 are flow-through pairs

# ---------------------------------------------------------------- 5 V -> 3.3 V
X, Y = 185.42, 104.14
note("+5V -> 3.3 V: AP7361C (1 A LDO) for the ESP32-S3 (WiFi peaks ~350 mA), the RTC and the 3.3 V sensor supply.", 170.18, 81.28)
part("C8", "Device:C", "22u 10V", C1206, (X, Y), {"1": "+5V", "2": "GND"})
part("C9", "Device:C", "100n", C0603, (X + 12.7, Y), {"1": "+5V", "2": "GND"})
part("U4", "Regulator_Linear:AP7361C-33E", "AP7361C-33E", "Package_TO_SOT_SMD:SOT-223-3_TabPin2", (X + 33.02, Y),
     {"VI": "+5V", "GND": "GND", "VO": "+3V3"})
part("C10", "Device:C", "22u 10V", C1206, (X + 53.34, Y), {"1": "+3V3", "2": "GND"})
part("C11", "Device:C", "100n", C0603, (X + 66.04, Y), {"1": "+3V3", "2": "GND"})
part("R7", "Device:R", "1k", R0603, (X + 81.28, Y - 2.54), {"1": "+3V3", "2": "LED_PWR_A"})
part("D5", "Device:LED", "PWR green", LED0805, (X + 93.98, Y + 2.54), {"A": "LED_PWR_A", "K": "GND"}, silk="PWR")

# ---------------------------------------------------------------- ESP32-S3-MINI-1-N8
X, Y = 101.6, 180.34
note("MCU: ESP32-S3-MINI-1-N8 (no PSRAM, so GPIO35-37 stay free). Pins per hardware/pinmap.json, board 'mini'.", 63.5, 134.62)
esp = {"3V3": "+3V3", "GND": "GND", "EN": "EN", "IO0": "PAIR",
       "IO1": "S1", "IO2": "S2", "IO3": "S3", "IO4": "S4",
       "IO10": "VIN_SENSE", "IO11": "BOARD_ID",
       "IO12": "I2C_INT_SDA", "IO13": "I2C_INT_SCL", "IO14": "I2C1_SDA", "IO15": "I2C1_SCL",
       "IO16": "RTC_INT", "USB_D-": "USB_DN", "USB_D+": "USB_DP",
       "IO35": "OUT1", "IO36": "OUT2", "TXD0": "TXD0", "RXD0": "RXD0", "IO46": "BUZZER", "IO48": "LED"}
part("U1", "RF_Module:ESP32-S3-MINI-1", "ESP32-S3-MINI-1-N8", "RF_Module:ESP32-S2-MINI-1", (X, Y), esp)
part("C12", "Device:C", "22u 10V", C1206, (X - 38.1, Y - 30.48), {"1": "+3V3", "2": "GND"})
part("C13", "Device:C", "100n", C0603, (X - 27.94, Y - 30.48), {"1": "+3V3", "2": "GND"})
part("R8", "Device:R", "10k", R0603, (X - 45.72, Y - 7.62), {"1": "+3V3", "2": "EN"})
part("C14", "Device:C", "1u", C0603, (X - 45.72, Y + 10.16), {"1": "EN", "2": "GND"})
part("SW1", "Switch:SW_Push", "RESET", BTN, (X - 45.72, Y + 25.4), {"1": "EN", "2": "GND"}, silk="RST")
part("R9", "Device:R", "10k", R0603, (X - 30.48, Y - 7.62), {"1": "+3V3", "2": "PAIR"})
part("SW2", "Switch:SW_Push", "PAIR / BOOT", BTN, (X - 30.48, Y + 25.4), {"1": "PAIR", "2": "GND"}, silk="PAIR")
# BOARD_ID: 30k/10k = 0.25 x 3V3 identifies a Mini (pinmap.json board_id); read once at boot on ADC2
part("R10", "Device:R", "30k 1%", R0603, (X + 48.26, Y - 20.32), {"1": "+3V3", "2": "BOARD_ID"})
part("R11", "Device:R", "10k 1%", R0603, (X + 48.26, Y + 2.54), {"1": "BOARD_ID", "2": "GND"})
# Status LED on GPIO48
part("R12", "Device:R", "1k", R0603, (X + 60.96, Y - 20.32), {"1": "LED", "2": "LED_STATUS_A"})
part("D6", "Device:LED", "STATUS blue", LED0805, (X + 60.96, Y + 2.54), {"A": "LED_STATUS_A", "K": "GND"}, silk="STATUS")
# Buzzer on GPIO46: a strapping pin that must read 0 at reset, so R14 holds the gate (and the pin) low
part("R13", "Device:R", "100", R0603, (X + 73.66, Y + 15.24), {"1": "BUZZER", "2": "BUZ_G"})
part("R14", "Device:R", "100k", R0603, (X + 73.66, Y + 30.48), {"1": "BUZZER", "2": "GND"})
part("Q1", "Transistor_FET:AO3400A", "AO3400A", SOT23, (X + 88.9, Y + 22.86), {"G": "BUZ_G", "S": "GND", "D": "BUZ_N"})
part("BZ1", "Device:Buzzer", "5V active 12mm", "Buzzer_Beeper:Buzzer_12x9.5RM7.6", (X + 99.06, Y + 7.62), {"+": "+5V", "-": "BUZ_N"})
part("D7", "Diode:1N4148W", "1N4148W", SOD123, (X + 119.38, Y + 7.62), {"K": "+5V", "A": "BUZ_N"})
# Production / debug header: the tester's pogo pins (same set as AWD1's J2)
part("J3", "Connector_Generic:Conn_01x08", "PROG", "Connector_PinHeader_1.27mm:PinHeader_1x08_P1.27mm_Vertical",
     (X + 48.26, Y + 38.1), {"1": "+3V3", "2": "GND", "3": "EN", "4": "PAIR", "5": "USB_DN", "6": "USB_DP",
                              "7": "TXD0", "8": "RXD0"})

# ---------------------------------------------------------------- RTC + internal I2C
X, Y = 236.22, 170.18
note("Internal I2C (GPIO12/13): DS3231MZ RTC at 0x68 + CR2032 (back side). INT/SQW on GPIO16 can wake the board.", 210.82, 134.62)
part("U5", "Timer_RTC:DS3231MZ", "DS3231MZ+", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", (X + 25.4, Y),
     {"VCC": "+3V3", "GND": "GND", "SDA": "I2C_INT_SDA", "SCL": "I2C_INT_SCL", "VBAT": "VBAT_RTC", "~{INT}/SQW": "RTC_INT"})
part("C15", "Device:C", "100n", C0603, (X, Y - 5.08), {"1": "+3V3", "2": "GND"})
part("BT1", "Device:Battery_Cell", "CR2032", "Battery:BatteryHolder_Keystone_3034_1x20mm", (X + 55.88, Y + 7.62),
     {"+": "VBAT_RTC", "-": "GND"}, side="back")
part("R15", "Device:R", "4.7k", R0603, (X - 2.54, Y + 20.32), {"1": "+3V3", "2": "I2C_INT_SDA"})
part("R16", "Device:R", "4.7k", R0603, (X + 10.16, Y + 20.32), {"1": "+3V3", "2": "I2C_INT_SCL"})
part("R17", "Device:R", "10k", R0603, (X + 22.86, Y + 20.32), {"1": "+3V3", "2": "RTC_INT"})

# ---------------------------------------------------------------- sensor ports S1-S4
note("Sensor ports S1-S4 (JST-XH 4-pin: 1 GND, 2 3V3, 3 5V, 4 SIG). SIG: 470R series + BAT54S clamps, so a 5 V digital signal is safe.", 20.32, 220.98)
note("Both port supplies are fused (F2, F3). Pull-ups a block needs (DS18B20 4.7k, DHT22 10k) live on the block or its adapter.", 20.32, 224.79)
part("F2", "Device:Polyfuse_Small", "0.5A hold", PTC1206, (25.4, 241.3), {"1": "+5V", "2": "+5V_PORT"})
part("F3", "Device:Polyfuse_Small", "0.2A hold", PTC1206, (25.4, 261.62), {"1": "+3V3", "2": "+3V3_PORT"})
part("C16", "Device:C", "10u", C0805, (38.1, 241.3), {"1": "+5V_PORT", "2": "GND"})
part("C17", "Device:C", "10u", C0805, (38.1, 261.62), {"1": "+3V3_PORT", "2": "GND"})
for i in range(1, 5):
    x = 30.48 + 38.1 * i
    part("J%d" % (3 + i), "Connector_Generic:Conn_01x04", "S%d" % i, XH4, (x + 15.24, 251.46),
         {"1": "GND", "2": "+3V3_PORT", "3": "+5V_PORT", "4": "S%d_EXT" % i}, silk="S%d" % i)
    part("R%d" % (17 + i), "Device:R", "470", R0603, (x - 7.62, 251.46), {"1": "S%d_EXT" % i, "2": "S%d" % i})
    part("D%d" % (7 + i), "Diode:BAT54S", "BAT54S", SOT23, (x - 7.62, 271.78), {"COM": "S%d" % i, "K": "+3V3", "A": "GND"})

# ---------------------------------------------------------------- student I2C port (Grove)
X, Y = 236.22, 241.3
note("I2C-1 student port, Grove pinout (1 SCL, 2 SDA, 3 3V3, 4 GND), on its own bus (GPIO14/15).", 210.82, 220.98)
part("J8", "Connector_Generic:Conn_01x04", "I2C-1 (Grove)", GROVE, (X + 30.48, Y + 10.16),
     {"1": "I2C1_SCL", "2": "I2C1_SDA", "3": "+3V3_PORT", "4": "GND"}, silk="I2C-1")
part("R22", "Device:R", "4.7k", R0603, (X, Y + 10.16), {"1": "+3V3", "2": "I2C1_SDA"})
part("R23", "Device:R", "4.7k", R0603, (X + 12.7, Y + 10.16), {"1": "+3V3", "2": "I2C1_SCL"})

# ---------------------------------------------------------------- relays OUT1, OUT2
note("Relays OUT1/OUT2 (GPIO35/36): Hongfa HF46F-005 (1 Form A, 5 A, 40 mA coil), low-side AO3400A, flyback diode, coil LED (shows through the lid).", 297.18, 81.28)
note("Terminals: 1 COM, 2 NO. Silkscreen: LOW VOLTAGE ONLY (switch a contactor coil, not a pump).", 297.18, 85.09)
for i in range(1, 3):
    y = 81.28 + 66.04 * i - 22.86
    o = "OUT%d" % i
    r = 23 + (i - 1) * 3
    part("R%d" % (r + 1), "Device:R", "100", R0603, (304.8, y), {"1": o, "2": o + "_G"})
    part("R%d" % (r + 2), "Device:R", "100k", R0603, (304.8, y + 15.24), {"1": o, "2": "GND"})
    part("Q%d" % (1 + i), "Transistor_FET:AO3400A", "AO3400A", SOT23, (320.04, y + 7.62), {"G": o + "_G", "S": "GND", "D": o + "_COIL"})
    part("K%d" % i, "ASC:HF46F", "HF46F/005-HS1", "ASC:Relay_SPST_Hongfa_HF46F", (350.52, y),
         {"1": "+5V", "2": o + "_COIL", "3": o + "_NO", "4": o + "_COM"})
    part("D%d" % (11 + i), "Diode:1N4148W", "1N4148W", SOD123, (332.74, y - 2.54), {"K": "+5V", "A": o + "_COIL"})
    part("R%d" % (r + 3), "Device:R", "1k", R0603, (370.84, y - 12.7), {"1": "+5V", "2": o + "_LED_A"})
    part("D%d" % (13 + i), "Device:LED", o + " red", LED0805, (370.84, y + 2.54), {"A": o + "_LED_A", "K": o + "_COIL"}, silk=o)
    part("J%d" % (8 + i), "Connector_Generic:Conn_01x02", o, TERM2, (393.7, y),
         {"1": o + "_COM", "2": o + "_NO"}, silk=o + "  COM NO")

# ---------------------------------------------------------------- mechanical
for i, xy in enumerate([(330.2, 254.0), (345.44, 254.0), (360.68, 254.0)], 1):
    part("H%d" % i, "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", xy, {})

PWR_FLAGS = [("GND", (20.32, 284.48)), ("+5V", (40.64, 284.48)), ("VIN", (60.96, 284.48)), ("VBAT_RTC", (81.28, 284.48))]

# ---- board geometry (architecture §8), for the board generator -------------------------------
BOARD_W, BOARD_H, CORNER_R = 97.37, 47.0, 15.0           # top corners R15, bottom square
HOLES = [(4.76, 13.45), (92.77, 13.45), (48.77, 31.52)]   # from the bottom-left corner, y up

def netname(n):
    return n if n in POWER_NETS else "/" + n
