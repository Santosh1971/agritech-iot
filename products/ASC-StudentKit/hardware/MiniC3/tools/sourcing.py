"""Sourcing BOM for the ASC Mini-C3 kit: one table -> sourcing/ASC-MiniC3-sourcing-BOM.xlsx and costing.csv.

Prices were checked on 2026-10-10 (LCSC prices converted at USD 1 = INR 88). "Quoted" means
read from the supplier page; "estimate" means a market estimate to confirm with the supplier.

    python3 tools/sourcing.py
"""
import csv, os
from openpyxl import Workbook
from openpyxl.styles import Alignment, Font, PatternFill
from openpyxl.utils import get_column_letter

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BATCH = 80          # kits per order: 20 panels x 4
USD = 88.0

def lcsc(code):
    return "https://www.lcsc.com/product-detail/%s.html" % code

# group, item, part / spec, refs, qty per kit, supplier, link, unit INR, basis, notes
ROWS = [
    ("PCB", "PCB panel share: 1 Mini-C3 + 1 AHT20 stick", "96 x 100 panel, 2 layers, 1.6 mm, HASL, V-cut (Cmts layer)", "-", 0.25,
     "WhyPCB (JLCPCB partner)", "https://www.whypcb.com/pcb-price-calculator/R0VSQkVSU1RPUkUtMzUzMA==", 150.0, "quoted",
     "Rs 3,000 for 20 panels = Rs 150 per panel; 4 kits per panel. Order fab/ASC-MiniC3-panel-gerbers.zip"),

    ("SMD (JLC assembly or LCSC)", "RTC DS1307 (MSKSEMI, pin compatible)", "DS1307Z+MSK SOIC-8, LCSC C49375271", "U2", 1,
     "LCSC", lcsc("C49375271"), round(0.2007 * USD, 1), "quoted", "Genuine Maxim DS1307Z+ (C1520446) is about Rs 240; the clone is enough here"),
    ("SMD (JLC assembly or LCSC)", "T/RH sensor AHT20 (on the stick)", "AHT20 DFN-6 3x3, LCSC C2757850", "U1 (stick)", 1,
     "LCSC", lcsc("C2757850"), 30.0, "quoted", "USD 0.32-0.65 at 10+ depending on stock lot; needs reflow (JLC or hot air)"),
    ("SMD (JLC assembly or LCSC)", "Schottky B5819W (flow level shift)", "SOD-123, LCSC C8598 (basic)", "D2", 1,
     "LCSC", lcsc("C8598"), round(0.0221 * USD, 1), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Driver ULN2003A (pump on ch 1+2, relay on ch 3)", "SOIC-16, TI ULN2003ADR, LCSC C7512", "U3", 1,
     "LCSC", lcsc("C7512"), round(0.1187 * USD, 1), "quoted", "Replaces 2 x AO3400A, 3 x B5819W and 4 resistors; same driver as FG1"),
    ("SMD (JLC assembly or LCSC)", "LED 1206 red (PUMP)", "YONGYUTAI YLED1206RFT, LCSC C37635751", "D3", 1, "LCSC", lcsc("C37635751"), round(0.0201 * USD, 1), "quoted", "1206 for hand soldering, as on WPC"),
    ("SMD (JLC assembly or LCSC)", "LED 1206 yellow (FLOW)", "YLED1206Y, LCSC C30584802", "D4", 1, "LCSC", lcsc("C30584802"), round(0.0161 * USD, 1), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "LED 1206 green (STATUS)", "YLED1206GFT, LCSC C37635752", "D5", 1, "LCSC", lcsc("C37635752"), round(0.0238 * USD, 1), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "LED 1206 blue (RELAY)", "YLED1206B, LCSC C28310438", "D7", 1, "LCSC", lcsc("C28310438"), round(0.0158 * USD, 1), "quoted", "Low stock at LCSC (450 on 2026-10-11)"),
    ("SMD (JLC assembly or LCSC)", "Resistor 0805 1k", "LCSC C17513", "R3, R4, R12", 3, "LCSC", lcsc("C17513"), round(0.0035 * USD, 2), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Resistor 0805 470R", "LCSC C17710", "R5", 1, "LCSC", lcsc("C17710"), round(0.0049 * USD, 2), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Resistor 0805 10k", "LCSC C17414", "R6, R7", 2, "LCSC", lcsc("C17414"), round(0.0034 * USD, 2), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Resistor 0805 4.7k", "LCSC C17673", "R8, R9", 2, "LCSC", lcsc("C17673"), round(0.0051 * USD, 2), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Capacitor 0805 100nF 50V", "LCSC C49678", "C2", 1, "LCSC", lcsc("C49678"), round(0.0141 * USD, 1), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "Capacitor 0805 10uF 25V (on the stick)", "LCSC C15850", "C1 (stick)", 1, "LCSC", lcsc("C15850"), round(0.0512 * USD, 1), "quoted", ""),
    ("SMD (JLC assembly or LCSC)", "JLC SMT assembly share (if JLC assembles)", "setup + stencil + 3 extended-part fees (DS1307, AHT20, ULN2003A) over 80 sets", "-", 1,
     "JLCPCB / WhyPCB", "https://jlcpcb.com/smt-assembly", 25.0, "estimate", "Skip if hand-assembled; the AHT20 still needs reflow or hot air"),

    ("Through-hole (India)", "ESP32-C3 Super Mini USB-C", "plugs into the two 1x8 sockets", "U1", 1,
     "Robosap", "https://robosap.in/product/esp32-c3-super-mini-usb-c-type-development-board/", 238.36, "quoted", "Rs 202 + GST; check the pin order against the 1:1 print"),
    ("Through-hole (India)", "Female header 1x8 2.54 mm", "Super Mini socket, 8.5 mm tall", "U1", 2,
     "Sharvi Electronics", "https://sharvielectronics.com/product/8x1-8pin-female-berg-strip-break-away-header-straight-2-54mm-pitch/", 10.72, "quoted", ""),
    ("Through-hole (India)", "Relay Hongfa HF46F 5 V, 5 A 250 VAC", "SPST-NO, sealed, VDE reinforced", "K1", 1,
     "techiesms", "https://techiesms.com/product/hf46f-5v-pcb-sealed-relaypack-of-5/", 42.0, "quoted", "Rs 210 for a pack of 5"),
    ("Through-hole (India)", "CR2032 with 3 solder tabs, vertical", "2 x + legs 15.2 mm apart, 1 x - leg", "BT1", 1,
     "Rajiv Electronics", "https://rajivelectronics.com/product/cr2032-3v-lithium-battery-vertical-3-pin-pc-mount-battery", 29.5, "quoted",
     "Measure the - leg offset on the first lot (footprint assumes 4.0 mm)"),
    ("Through-hole (India)", "ALTERNATIVE: CR2032 holder (vertical, 3 pins, Rajiv RBH-2) + loose CR2032 cell", "3-pin vertical holder; pin positions to be measured against the tabbed-cell pads", "BT1 (alt)", 0,
     "Rajiv Electronics", "https://rajivelectronics.com/product/cr2032-3v-pcb-mount-vertical-battery-holder", 11.8, "quoted",
     "Rs 11.80 + a loose CR2032 (about Rs 20). Replaceable cell. Measure a sample: if its 3 pins match the tabbed-cell pads it drops in, otherwise we add its pads"),
    ("Through-hole (India)", "Crystal 32.768 kHz 2x6 mm, 12.5 pF", "cylindrical", "Y1", 1,
     "Magno Teknik", "https://store.magnoteknik.com/product/mtcr2632r768k/", 14.0, "quoted", "Rs 14 at 100+; local market about Rs 4-8"),
    ("Through-hole (India)", "Electrolytic 220 uF 10 V, 5 mm dia, 2.0 mm pitch", "radial 5 x 11", "C1", 1,
     "Local market", "", 2.0, "estimate", ""),
    ("Through-hole (India)", "Screw terminal 3.5 mm 2P (PUMP)", "KF350-2P / DG350 (Phoenix PT-1,5 footprint), horizontal", "J1", 1,
     "Local market (Lamington Rd / SP Road)", "", 4.0, "estimate", "3.5 mm pitch blocks interlock: buy 2P and 3P and join them"),
    ("Through-hole (India)", "Screw terminal 3.5 mm 4P (I2C)", "KF350 2P + 2P joined", "J5", 1, "Local market", "", 8.0, "estimate", "Wire entry faces the board edge"),
    ("Through-hole (India)", "Screw terminal 3.5 mm 5P (AUX)", "KF350 2P + 3P joined", "J3", 1, "Local market", "", 10.0, "estimate", ""),
    ("Through-hole (India)", "Screw terminal 3.5 mm 3P (FLOW, field builds)", "KF350-3P; fits the same holes as the JST-XH", "J4 (alt)", 0,
     "Local market", "", 6.0, "estimate", "FG1 / long cable: fit this instead of the JST-XH header"),
    ("Through-hole (India)", "Pluggable header 5.08 mm 2P, vertical, + plug (RELAY 230 V)", "MSTBVA 2,5/2-G-5,08 type (2EDG-V 5.08) with 2EDGK plug", "J6", 1,
     "Local market", "https://www.electronicscomp.com/kf301-2-pin-5.08mm-pitch-plug-in-screw-terminal-block-connector", 15.0, "estimate",
     "Same pluggable family as FG1's relay connector; buy header and plug as a pair (link: similar plug-in block)"),
    ("Through-hole (India)", "JST-XH 3-pin male header, straight 2.5 mm (FLOW, student builds)", "B3B-XH-A type; shares the footprint with the 3.5 mm terminal", "J4", 1,
     "DNA Technology", "https://www.dnatechindia.com/3-pin-2-50mm-jst-xh-straight-male-connector-buy-in-india.html", 4.56, "quoted", "Mates with the YF-S401 plug"),
    ("Through-hole (India)", "Pin header 1x4 2.54 mm (AHT20 stick cable end, optional)", "or solder the cable directly", "J1 (stick)", 0,
     "Local market", "", 1.0, "estimate", "Not needed if the cable is soldered"),

    ("Kit items", "Flow sensor YF-S401 0.3-6 L/min", "5880 pulses per litre, JST-XH lead", "-", 1,
     "AM Electronics", "https://amelectronics.in/product/water-flow-sensor-yf-s401-flowmeter-0-3-6l-min-3-5mm-white/", 230.0, "quoted", "Check the plug order: GND, 5V, SIG"),
    ("Kit items", "Mini submersible pump 3-9 V DC", "for 5 V, about 0.2-0.3 A", "-", 1,
     "QuartzComponents", "https://quartzcomponents.com/products/3v-to-12v-mini-dc-submersible-pump", 33.0, "quoted",
     "Out of stock on 2026-10-10; U-nozzle version Rs 61: quartzcomponents.com/collections/dc-motors/products/3v-to-9v-mini-dc-submersible-pump-u-type-nozzel"),
    ("Kit items", "Enclosure REI IP-65-26 (TR top) 90x58x44", "clear lid", "-", 1,
     "REI Cabinets", "https://www.reicabinets.com/product?cid=16&pid=479&prodn=IP-65-26%20(TR%20Top)", 95.0, "quoted", ""),
    ("Kit items", "Nylon reverse-mount snap standoff 5 mm", "base taped to the box floor, head through the 4.2 mm PCB hole", "H1-H3", 3,
     "Probots", "https://probots.co.in/5mm-nylon-reverse-mount-pcb-standoff-spacer.html", 9.4, "quoted", "Rs 9.40 at 50+, Rs 8.50 at 100+"),
    ("Kit items", "Double-sided foam tape (3M VHB type) squares", "about 10 x 10 mm, one per standoff", "-", 3, "Local market", "", 1.0, "estimate", ""),
    ("Kit items", "Cable gland PG7 nylon (3-6 mm cable)", "USB, sensors, pump", "-", 3,
     "ElectroHeight", "https://electroheight.com/PG7-cable-gland", 2.05, "quoted", "Rs 205 per 100; single pieces about Rs 12 (dnatechindia.com/pg7-cable-gland-buy-in-india.html)"),
    ("Kit items", "4-core cable 1 m (AHT20 stick)", "e.g. 4 x 0.25 sq mm", "-", 1, "Local market", "", 15.0, "estimate", ""),
    ("Kit items", "Silicone tube 1 m for the pump", "match the pump outlet (about 7.5 mm OD)", "-", 1, "Local market", "", 30.0, "estimate", ""),
]

# ---- sample order: 2 kits for testing (PCBs already ordered: 5 panels = 20 Minis + 20 sticks)
SAMPLE_KITS = 2
U = USD
# item -> (order qty, unit Rs at that qty, why this qty)
SAMPLE = {
    "PCB panel share: 1 Mini-C3 + 1 AHT20 stick": (0, 0.0, "Already ordered: 5 panels = 20 Minis + 20 sticks"),
    "RTC DS1307 (MSKSEMI, pin compatible)": (5, round(0.2544 * U, 1), "LCSC minimum 5"),
    "T/RH sensor AHT20 (on the stick)": (4, round(0.7638 * U, 1), "2 + 2 spare (3 x 3 mm part, hot-air soldering)"),
        "Schottky B5819W (flow level shift)": (20, round(0.0283 * U, 1), "LCSC minimum 20"),
    "Driver ULN2003A (pump on ch 1+2, relay on ch 3)": (5, round(0.1547 * U, 1), "LCSC minimum 5"),
    "LED 1206 red (PUMP)": (50, round(0.0201 * U, 2), "LCSC minimum 50"),
    "LED 1206 yellow (FLOW)": (50, round(0.0161 * U, 2), "LCSC minimum 50"),
    "LED 1206 green (STATUS)": (20, round(0.0238 * U, 2), "LCSC minimum 20"),
    "LED 1206 blue (RELAY)": (50, round(0.0158 * U, 2), "LCSC minimum 50"),
    "Resistor 0805 1k": (100, round(0.0035 * U, 2), "LCSC minimum 100"),
    "Resistor 0805 470R": (100, round(0.0049 * U, 2), "LCSC minimum 100"),
    "Resistor 0805 10k": (100, round(0.0034 * U, 2), "LCSC minimum 100"),
    "Resistor 0805 4.7k": (100, round(0.0051 * U, 2), "LCSC minimum 100"),
    "Capacitor 0805 100nF 50V": (20, round(0.0191 * U, 2), "LCSC minimum 20"),
    "Capacitor 0805 10uF 25V (on the stick)": (20, round(0.0695 * U, 2), "LCSC minimum 20"),
    "JLC SMT assembly share (if JLC assembles)": (0, 0.0, "Not for samples: hand-assemble (0805 parts; AHT20 with hot air)"),
    "ESP32-C3 Super Mini USB-C": (3, 238.36, "2 + 1 spare"),
    "Female header 1x8 2.54 mm": (4, 10.72, ""),
    "Relay Hongfa HF46F 5 V, 5 A 250 VAC": (5, 42.0, "Sold as a pack of 5 (Rs 210)"),
    "CR2032 with 3 solder tabs, vertical": (3, 29.5, "2 + 1 to measure the leg positions"),
    "ALTERNATIVE: CR2032 holder (vertical, 3 pins, Rajiv RBH-2) + loose CR2032 cell": (2, 11.8, "Samples only, to measure the pins and judge quality"),
    "Crystal 32.768 kHz 2x6 mm, 12.5 pF": (3, 15.0, "2 + 1 spare"),
    "Electrolytic 220 uF 10 V, 5 mm dia, 2.0 mm pitch": (2, 3.0, ""),
    "Screw terminal 3.5 mm 2P (PUMP)": (8, 4.0, "Per kit: PUMP 2P + I2C 2P+2P + the 2P half of AUX = 4 x 2P"),
    "Screw terminal 3.5 mm 4P (I2C)": (0, 0.0, "Made from 2 x 2P (counted in the 2P line)"),
    "Screw terminal 3.5 mm 5P (AUX)": (2, 6.0, "3P half of the 5P (the 2P half is in the 2P line)"),
    "Screw terminal 3.5 mm 3P (FLOW, field builds)": (2, 6.0, "For testing the field (terminal) build of the flow input"),
    "Pluggable header 5.08 mm 2P, vertical, + plug (RELAY 230 V)": (2, 15.0, "Header + plug pairs"),
    "JST-XH 3-pin male header, straight 2.5 mm (FLOW, student builds)": (3, 4.56, "2 + 1 spare"),
    "Pin header 1x4 2.54 mm (AHT20 stick cable end, optional)": (0, 0.0, "Solder the cable directly"),
    "Flow sensor YF-S401 0.3-6 L/min": (2, 230.0, ""),
    "Mini submersible pump 3-9 V DC": (2, 61.0, "Rs 33 version out of stock; U-nozzle version Rs 61"),
    "Enclosure REI IP-65-26 (TR top) 90x58x44": (2, 95.0, ""),
    "Nylon reverse-mount snap standoff 5 mm": (8, 10.0, "6 + 2 spare (Rs 10 below 25 pcs)"),
    "Double-sided foam tape (3M VHB type) squares": (1, 60.0, "One small roll"),
    "Cable gland PG7 nylon (3-6 mm cable)": (6, 11.8, "Single pieces (DNA Technology Rs 11.80)"),
    "4-core cable 1 m (AHT20 stick)": (2, 15.0, "2 m"),
    "Silicone tube 1 m for the pump": (2, 30.0, "2 m"),
}

def sample_sheet(wb):
    ws = wb.create_sheet("Sample - 2 kits", 0)
    ws.append(["ASC Mini-C3: sample order for %d test kits (prices checked 2026-10-10, LCSC at USD 1 = Rs %d)" % (SAMPLE_KITS, USD)])
    ws.append(["PCBs are already ordered (5 panels = 20 Minis + 20 sticks). Order qty follows each supplier's minimum or pack size, "
               "so the cost is higher than 2 x the per-kit cost. 'estimate' = confirm with the supplier."])
    ws.append(["LCSC: one combined order; expect about Rs 1,800-2,500 shipping + duty to India for a small parcel. "
               "Alternative for 2 kits: buy the 0805 resistors / capacitors / LEDs locally and only the DS1307 + AHT20 from LCSC."])
    ws.append([])
    head = ["#", "Group", "Item", "Part / spec", "Ref", "Need for %d kits" % SAMPLE_KITS, "Order qty", "Supplier", "Link",
            "Unit Rs", "Line Rs", "Price", "Why this qty"]
    ws.append(head)
    hr = ws.max_row
    for c in ws[hr]:
        c.font = Font(bold=True, color="FFFFFF"); c.fill = PatternFill("solid", fgColor="1E5B3E"); c.alignment = Alignment(wrap_text=True, vertical="top")
    ws["A1"].font = Font(bold=True, size=13)
    first = hr + 1
    n = 0
    for g, item, spec, ref, q, sup, link, unit, basis, notes in ROWS:
        oq, ou, why = SAMPLE[item]
        need = q * SAMPLE_KITS
        if oq == 0 and need == 0:
            continue
        n += 1
        r = ws.max_row + 1
        ws.append([n, g, item, spec, ref, need, oq, sup, link or "", ou, None, basis, why])
        ws.cell(r, 11).value = "=ROUND(G%d*J%d,0)" % (r, r)
        if link:
            ws.cell(r, 9).hyperlink = link
            ws.cell(r, 9).font = Font(color="1155CC", underline="single")
        if basis == "estimate":
            ws.cell(r, 12).fill = PatternFill("solid", fgColor="FBEFD9")
        if oq == 0:
            for c in ws[r]:
                c.font = Font(color="888888", italic=True)
        for c in ws[r]:
            c.alignment = Alignment(wrap_text=True, vertical="top")
    last = ws.max_row
    ws.append([])
    for label, grp in (("LCSC order (SMD parts)", ("SMD (JLC assembly or LCSC)",)), ("Indian suppliers / local", ("Through-hole (India)", "Kit items"))):
        cond = "+".join('SUMIF(B%d:B%d,"%s",K%d:K%d)' % (first, last, g, first, last) for g in grp)
        r = ws.max_row + 1
        ws.append(["", "", label, "", "", "", "", "", "", "", "=" + cond])
        ws.cell(r, 3).font = Font(bold=True); ws.cell(r, 11).font = Font(bold=True)
    r = ws.max_row + 1
    ws.append(["", "", "TOTAL parts for %d sample kits (+ LCSC shipping)" % SAMPLE_KITS, "", "", "", "", "", "", "", "=SUM(K%d:K%d)" % (first, last)])
    ws.cell(r, 3).font = Font(bold=True, size=12); ws.cell(r, 11).font = Font(bold=True, size=12)
    for i, w in enumerate([4, 20, 36, 32, 11, 9, 9, 22, 46, 9, 9, 9, 46], 1):
        ws.column_dimensions[get_column_letter(i)].width = w
    ws.freeze_panes = ws.cell(hr + 1, 3)
    tot = {}
    for g, item, spec, ref, q, sup, link, unit, basis, notes in ROWS:
        oq, ou, why = SAMPLE[item]
        k = "lcsc" if g.startswith("SMD") else "local"
        tot[k] = tot.get(k, 0) + oq * ou
    return tot

def main():
    wb = Workbook()
    ws = wb.active
    ws.title = "Sourcing BOM"
    head = ["#", "Group", "Item", "Part / spec", "Ref", "Qty / kit", "Qty for %d kits" % BATCH, "Supplier", "Link",
            "Unit Rs", "Rs / kit", "Rs for %d kits" % BATCH, "Price", "Notes"]
    ws.append(["ASC Mini-C3 kit: sourcing BOM (prices checked 2026-10-10, LCSC at USD 1 = Rs %d)" % USD])
    ws.append(["Batch of %d kits = 20 panels x 4. Rs / kit = unit x qty. 'quoted' = read from the supplier page; 'estimate' = confirm with the supplier." % BATCH])
    ws.append([])
    ws.append(head)
    hr = ws.max_row
    for c in ws[hr]:
        c.font = Font(bold=True, color="FFFFFF"); c.fill = PatternFill("solid", fgColor="1E5B3E"); c.alignment = Alignment(wrap_text=True, vertical="top")
    ws["A1"].font = Font(bold=True, size=13)
    first = hr + 1
    for i, (g, item, spec, ref, q, sup, link, unit, basis, notes) in enumerate(ROWS, 1):
        r = ws.max_row + 1
        ws.append([i, g, item, spec, ref, q, None, sup, link or "", unit, None, None, basis, notes])
        ws.cell(r, 7).value = "=CEILING(F%d*%d,1)" % (r, BATCH)
        ws.cell(r, 11).value = "=ROUND(F%d*J%d,2)" % (r, r)
        ws.cell(r, 12).value = "=ROUND(F%d*%d*J%d,0)" % (r, BATCH, r)
        if link:
            ws.cell(r, 9).hyperlink = link
            ws.cell(r, 9).font = Font(color="1155CC", underline="single")
        if basis == "estimate":
            ws.cell(r, 13).fill = PatternFill("solid", fgColor="FBEFD9")
        for c in ws[r]:
            c.alignment = Alignment(wrap_text=True, vertical="top")
    last = ws.max_row
    ws.append([])
    for label, grp in (("Subtotal PCB + board parts", ("PCB", "SMD (JLC assembly or LCSC)", "Through-hole (India)")), ("Subtotal kit items", ("Kit items",))):
        r = ws.max_row + 1
        cond = "+".join('SUMIF(B%d:B%d,"%s",K%d:K%d)' % (first, last, g, first, last) for g in grp)
        ws.append(["", "", label, "", "", "", "", "", "", "", "=" + cond, "=" + cond.replace("K%d:K%d" % (first, last), "L%d:L%d" % (first, last))])
        ws.cell(r, 3).font = Font(bold=True); ws.cell(r, 11).font = Font(bold=True); ws.cell(r, 12).font = Font(bold=True)
    r = ws.max_row + 1
    ws.append(["", "", "TOTAL per kit / per batch", "", "", "", "", "", "", "", "=SUM(K%d:K%d)" % (first, last), "=SUM(L%d:L%d)" % (first, last)])
    for c in (3, 11, 12):
        ws.cell(r, c).font = Font(bold=True, size=12)
    widths = [4, 20, 36, 34, 12, 8, 10, 22, 48, 9, 9, 11, 9, 50]
    for i, w in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(i)].width = w
    ws.freeze_panes = ws.cell(hr + 1, 3)
    ws.title = "Production - %d kits" % BATCH
    stot = sample_sheet(wb)
    os.makedirs(os.path.join(PRJ, "sourcing"), exist_ok=True)
    path = os.path.join(PRJ, "sourcing", "ASC-MiniC3-sourcing-BOM.xlsx")
    wb.save(path)
    print("sample (2 kits): LCSC Rs %.0f + local Rs %.0f = Rs %.0f (+ LCSC shipping)" % (stot["lcsc"], stot["local"], stot["lcsc"] + stot["local"]))

    # costing.csv in the old format, from the same rows
    tot = {"board": 0.0, "kit": 0.0}
    with open(os.path.join(PRJ, "costing.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Group", "Item", "Ref", "Qty", "Unit INR", "Line INR", "Source / basis", "Status"])
        for g, item, spec, ref, q, sup, link, unit, basis, notes in ROWS:
            grp = "Kit" if g == "Kit items" else "Board"
            line = round(q * unit, 2)
            tot["kit" if grp == "Kit" else "board"] += line
            w.writerow([grp, item, ref, q, unit, line, "%s %s" % (sup, link), basis])
    print("%s\nboard + parts Rs %.0f, kit items Rs %.0f, total Rs %.0f per kit" % (path, tot["board"], tot["kit"], tot["board"] + tot["kit"]))

if __name__ == "__main__":
    main()
