"""Checks the generated netlist (tools/netlist.json) before KiCad's ERC:

- every ESP32 pin agrees with ../../pinmap.json, board "mini";
- every signal net joins at least two pads (a lone label is a typo);
- every power net reaches the parts that need it;
- strapping pin GPIO46 has its pull-down, GPIO0 its pull-up.

Run after gen_sch.py:  python3 tools/check_design.py
"""
import collections, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
net = json.load(open(os.path.join(HERE, "netlist.json")))
pinmap = json.load(open(os.path.join(HERE, "../../pinmap.json")))["boards"]["mini"]["pins"]

# The module's pads -> GPIO, from the ESP32-S3-MINI-1 symbol.
PAD_GPIO = {"4": 0, "5": 1, "6": 2, "7": 3, "8": 4, "9": 5, "10": 6, "11": 7, "12": 8, "13": 9, "14": 10, "15": 11,
            "16": 12, "17": 13, "18": 14, "19": 15, "20": 16, "21": 17, "22": 18, "23": 19, "24": 20, "25": 21,
            "26": 26, "28": 33, "29": 34, "31": 35, "32": 36, "33": 37, "34": 38, "35": 39, "36": 40, "37": 41,
            "38": 42, "39": 43, "40": 44, "41": 45, "44": 46, "27": 47, "30": 48}
# pinmap.json names -> net names in design.py
NAME = {"PAIR_BTN": "PAIR", "USB_D-": "USB_DN", "USB_D+": "USB_DP", "I2C_EXT_SDA": "I2C1_SDA", "I2C_EXT_SCL": "I2C1_SCL",
        "DEBUG_TX": "TXD0", "DEBUG_RX": "RXD0"}

errors = []
u1 = next(p for p in net if p["ref"] == "U1")
for pad, gpio in PAD_GPIO.items():
    want = pinmap.get(str(gpio))
    got = u1["pads"].get(pad, "").lstrip("/")
    if want and NAME.get(want, want) != got:
        errors.append("GPIO%d: pinmap.json says %s, the schematic has %s" % (gpio, want, got or "nothing"))
    if not want and got:
        errors.append("GPIO%d carries %s, but pinmap.json has it free" % (gpio, got))

members = collections.defaultdict(list)
for p in net:
    for pad, n in p["pads"].items():
        members[n].append("%s.%s" % (p["ref"], pad))
for n, pads in sorted(members.items()):
    if len({x.split(".")[0] for x in pads}) < 2:
        errors.append("net %s reaches only %s" % (n, ", ".join(pads)))

def parts_on(n):
    return {x.split(".")[0] for x in members.get(n, [])}
for n, need in {"+3V3": {"U1", "U5", "U4"}, "+5V": {"U4", "K1", "K2", "BZ1"}, "GND": {"U1", "U2", "U4", "U5", "J1", "J2"}}.items():
    miss = need - parts_on(n)
    if miss:
        errors.append("%s does not reach %s" % (n, ", ".join(sorted(miss))))
if "R14" not in parts_on("/BUZZER") or "GND" not in next(p for p in net if p["ref"] == "R14")["pads"].values():
    errors.append("GPIO46 (BUZZER) needs its pull-down to GND (strapping pin)")
if "R9" not in parts_on("/PAIR"):
    errors.append("GPIO0 (PAIR) needs its pull-up")

print("%d parts, %d nets" % (len(net), len(members)))
if errors:
    print("\n".join("FAIL: " + e for e in errors))
    sys.exit(1)
print("OK: pins match pinmap.json, no single-pad nets, power reaches every part, strapping pins set")
