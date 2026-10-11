"""TH2 field node v0.2 - single source of truth for parts and nets.

Low-cost T/RH monitor on ONE 40 x 60 mm PCB in the common AWD/TH box: ESP32-C3-WROOM-02,
LiFePO4 14500 (AA holder on the PCB), CN3058E charger, RT9080 LDO, 32.768 kHz RTC crystal,
optional SX1262 LoRa. Back side: SHT40 + PAIR button + charge LEDs only (under the front-shell
vent / membrane / light window).

Net names follow KiCad's convention for local labels on the root sheet ("/NAME");
power nets (GND, +3V3) are global.
"""

PROJECT = "TH2-FieldNode"
POWER_NETS = {"GND", "+3V3"}

# ---- parts --------------------------------------------------------------------
# (ref, lib_id, value, footprint, sch_xy, {pin (number or name): net})
# Unlisted pins are marked no-connect in the schematic.
P = []
def part(ref, lib, val, fp, xy, pins, **kw):
    P.append(dict(ref=ref, lib=lib, value=val, fp=fp, xy=xy, pins=pins, **kw))

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
LED0603 = "LED_SMD:LED_0603_1608Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
JST4 = "Connector_JST:JST_PH_S4B-PH-SM4-TB_1x04-1MP_P2.00mm_Horizontal"

# ---- USB-C input: charging + native USB flashing --------------------------------
part("J1", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C 16P", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
     (38.1, 76.2), {"VBUS": "VBUS_IN", "GND": "GND", "SHIELD": "GND", "CC1": "CC1", "CC2": "CC2",
                    "D-": "USB_DN", "D+": "USB_DP"})
part("R1", "Device:R", "5.1k", R0603, (71.12, 99.06), {"1": "CC1", "2": "GND"})
part("R2", "Device:R", "5.1k", R0603, (81.28, 99.06), {"1": "CC2", "2": "GND"})
# Alternative input: 4-pin magnetic pogo panel connector in the enclosure wall, wired to J2
part("J2", "Connector_Generic:Conn_01x04", "MAG-POGO", JST4, (35.56, 132.08),
     {"1": "VBUS_IN", "2": "USB_DN", "3": "USB_DP", "4": "GND"})
part("F1", "Device:Polyfuse", "0.75A", "Fuse:Fuse_1206_3216Metric", (101.6, 55.88), {"1": "VBUS_IN", "2": "VBUS"})
part("U4", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", (101.6, 124.46),
     {"1": "USB_DN", "6": "USB_DN", "3": "USB_DP", "4": "USB_DP", "VBUS": "VBUS", "GND": "GND"})

# ---- LiFePO4 charger: CN3058E, 3.6 V CV, ICH = 1218 V / RISET = 0.31 A (0.5C for a 600 mAh 14500)
part("U2", "TH2:CN3058E", "CN3058E", "Package_SO:SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.3mm_ThermalVias",
     (160.02, 76.2), {"VIN": "VBUS", "CE": "VBUS", "TEMP": "TEMP", "ISET": "ISET", "GND": "GND", "EP": "GND",
                      "BAT": "VBAT", "~{CHRG}": "CHRG_N", "~{DONE}": "DONE_N"})
part("C1", "Device:C", "10u", C0805, (127.0, 58.42), {"1": "VBUS", "2": "GND"})
part("R3", "Device:R", "3.9k", R0603, (132.08, 93.98), {"1": "ISET", "2": "GND"})
part("R4", "Device:R", "0R", R0603, (121.92, 93.98), {"1": "TEMP", "2": "GND"})
part("C2", "Device:C", "10u", C0805, (193.04, 58.42), {"1": "VBAT", "2": "GND"})
part("R5", "Device:R", "1k", R0603, (187.96, 99.06), {"1": "VBUS", "2": "CHG_A"})
part("D2", "Device:LED", "RED (back)", LED0603, (187.96, 121.92), {"A": "CHG_A", "K": "CHRG_N"})
part("R6", "Device:R", "1k", R0603, (213.36, 99.06), {"1": "VBUS", "2": "DONE_A"})
part("D3", "Device:LED", "GREEN (back)", LED0603, (213.36, 121.92), {"A": "DONE_A", "K": "DONE_N"})

# ---- Battery: LiFePO4 14500 (AA size) in a PCB-mount holder + P-FET reverse protection ----
part("BT1", "Device:Battery_Cell", "IFR14500 600mAh", "Battery:BatteryHolder_Keystone_2460_1xAA",
     (233.68, 71.12), {"+": "VBAT_RAW", "-": "GND"})
part("Q1", "Transistor_FET:AO3401A", "AO3401A", SOT23, (266.7, 63.5), {"G": "Q1_G", "S": "VBAT", "D": "VBAT_RAW"})
part("R7", "Device:R", "100k", R0603, (266.7, 86.36), {"1": "Q1_G", "2": "GND"})
part("C3", "Device:C", "47u", C1206, (279.4, 58.42), {"1": "VBAT", "2": "GND"})

# ---- 3V3 LDO: RT9080-33 (2 uA Iq). Needed because the charger holds VBAT at 3.6 V,
#      the ESP32-C3 absolute maximum. Same SOT-23-5 pinout as AP2112K.
part("U3", "Regulator_Linear:AP2112K-3.3", "RT9080-33GJ5", "Package_TO_SOT_SMD:SOT-23-5", (304.8, 63.5),
     {"VIN": "VBAT", "EN": "VBAT", "GND": "GND", "VOUT": "+3V3"})
part("C4", "Device:C", "10u", C0805, (330.2, 58.42), {"1": "+3V3", "2": "GND"})
part("C5", "Device:C", "100n", C0603, (342.9, 58.42), {"1": "+3V3", "2": "GND"})

# ---- Battery sense: 2.2M/2.2M divider, always on (0.8 uA) - IO4 goes to LoRa DIO1 ----
part("R8", "Device:R", "2.2M", R0603, (373.38, 55.88), {"1": "VBAT", "2": "VBAT_ADC"})
part("R9", "Device:R", "2.2M", R0603, (373.38, 83.82), {"1": "VBAT_ADC", "2": "GND"})
part("C6", "Device:C", "100n", C0603, (358.14, 71.12), {"1": "VBAT_ADC", "2": "GND"})
# Charger-present detect (wakes the C3 from deep sleep; 0 uA when unplugged)
part("R11", "Device:R", "100k", R0603, (350.52, 106.68), {"1": "VBUS", "2": "VBUS_DET"})
part("R12", "Device:R", "100k", R0603, (350.52, 139.7), {"1": "VBUS_DET", "2": "GND"})

# ---- MCU: ESP32-C3-WROOM-02 ------------------------------------------------------
esp = {"3V3": "+3V3", "GND": "GND", "EN": "EN",
       "IO0": "XTAL32_P", "IO1": "XTAL32_N", "IO2": "LORA_NSS", "IO3": "VBAT_ADC", "IO4": "LORA_DIO1",
       "IO5": "VBUS_DET", "IO6": "SDA", "IO7": "SCL", "IO8": "LORA_BUSY", "IO9": "BOOT", "IO10": "LORA_SCK",
       "IO18": "USB_DN", "IO19": "USB_DP", "IO20/RXD": "LORA_MISO", "IO21/TXD": "LORA_MOSI"}
part("U1", "RF_Module:ESP32-C3-WROOM-02", "ESP32-C3-WROOM-02-N4", "RF_Module:ESP32-C3-WROOM-02",
     (203.2, 205.74), esp)
part("C7", "Device:C", "10u", C0805, (139.7, 175.26), {"1": "+3V3", "2": "GND"})
part("C8", "Device:C", "100n", C0603, (152.4, 175.26), {"1": "+3V3", "2": "GND"})
part("R13", "Device:R", "10k", R0603, (40.64, 170.18), {"1": "+3V3", "2": "EN"})
part("C9", "Device:C", "1u", C0603, (40.64, 190.5), {"1": "EN", "2": "GND"})
part("R14", "Device:R", "10k", R0603, (58.42, 170.18), {"1": "+3V3", "2": "BOOT"})
part("SW1", "Switch:SW_Push", "PAIR/BOOT (back)", "Button_Switch_SMD:SW_SPST_TL3342", (58.42, 193.04), {"1": "BOOT", "2": "GND"})
# Strap pull-ups: IO2 (= LoRa NSS, also keeps the radio deselected) and IO8 (= LoRa BUSY)
part("R15", "Device:R", "10k", R0603, (76.2, 170.18), {"1": "+3V3", "2": "LORA_NSS"})
part("R16", "Device:R", "10k", R0603, (93.98, 170.18), {"1": "+3V3", "2": "LORA_BUSY"})

# 32.768 kHz crystal on XTAL_32K_P/N (GPIO0/GPIO1) - RTC timebase, ~20 ppm vs 5 % internal RC
part("Y1", "Device:Crystal", "32.768kHz 12.5pF", "Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm", (66.04, 233.68),
     {"1": "XTAL32_P", "2": "XTAL32_N"})
part("C10", "Device:C", "12p", C0603, (53.34, 256.54), {"1": "XTAL32_P", "2": "GND"})
part("C11", "Device:C", "12p", C0603, (78.74, 256.54), {"1": "XTAL32_N", "2": "GND"})

# ---- SHT40 on the BACK side, in a milled-slot thermal island under the front-shell vent ----
part("R18", "Device:R", "4.7k", R0603, (271.78, 170.18), {"1": "+3V3", "2": "SDA"})
part("R19", "Device:R", "4.7k", R0603, (281.94, 170.18), {"1": "+3V3", "2": "SCL"})
part("U5", "Sensor_Humidity:SHT4x", "SHT40-AD1B (back)", "Sensor_Humidity:Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad",
     (304.8, 205.74), {"VDD": "+3V3", "VSS": "GND", "SDA": "SDA", "SCL": "SCL"})
part("C12", "Device:C", "100n (back)", C0603, (325.12, 200.66), {"1": "+3V3", "2": "GND"})

# ---- Production jig pads: flash + log over native USB (UART0 pins now carry LoRa SPI) ----
part("J4", "Connector_Generic:Conn_01x06", "PROG", "Connector_PinHeader_1.27mm:PinHeader_1x06_P1.27mm_Vertical",
     (299.72, 236.22), {"1": "+3V3", "2": "GND", "3": "EN", "4": "BOOT", "5": "USB_DN", "6": "USB_DP"})

# ---- LoRa option (TH2-L only; leave U6/AE1/C13/C14 unfitted on TH2-W) ---------------------
#      Same ISC-SX1262-B module + Ra-01 footprint as WPC/AWD. NRESET tied to the C3's EN.
part("U6", "TH2:ISC-SX1262-B", "ISC-SX1262-B", "RF_Module:Ai-Thinker-Ra-01-LoRa", (154.94, 256.54),
     {"VDD": "+3V3", "GND": "GND", "MOSI": "LORA_MOSI", "MISO": "LORA_MISO", "SCK": "LORA_SCK",
      "~{NSS}": "LORA_NSS", "~{RESET}": "EN", "DIO1": "LORA_DIO1", "DIO4/BUSY": "LORA_BUSY", "ANT": "RF_ANT"})
part("AE1", "Device:Antenna", "866MHz spring", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", (205.74, 248.92),
     {"1": "RF_ANT"})
part("C13", "Device:C", "10u", C0805, (109.22, 256.54), {"1": "+3V3", "2": "GND"})
part("C14", "Device:C", "100n", C0603, (121.92, 256.54), {"1": "+3V3", "2": "GND"})

PWR_FLAGS = [("GND", (200.66, 147.32)), ("VBUS", (213.36, 147.32)), ("VBAT", (226.06, 147.32))]

def netname(n):
    return n if n in POWER_NETS else "/" + n
