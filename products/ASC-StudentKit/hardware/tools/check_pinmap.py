"""Check the ASC Student Kit pin map (../pinmap.json) for mistakes.

This is the "rules decide" half of the studio's architecture stage, run on
our own kits first. Exits non-zero and lists every problem it finds.

    python3 tools/check_pinmap.py
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.dirname(HERE)


def main():
    with open(os.path.join(HW, "pinmap.json")) as f:
        pm = json.load(f)
    rules = pm["rules"]
    module = set(pm["module_gpios"])
    adc1, adc2 = set(pm["adc1"]), set(pm["adc2"])
    errors = []

    for name, b in pm["boards"].items():
        pins = {int(g): sig for g, sig in b["pins"].items()}
        where = {}
        for g, sig in pins.items():
            if g not in module:
                errors.append(f"{name}: GPIO{g} ({sig}) is not a pin on {pm['module']}")
            if sig in where:
                errors.append(f"{name}: {sig} is on both GPIO{where[sig]} and GPIO{g}")
            where[sig] = g
            if str(g) in pm["usb"] and sig != pm["usb"][str(g)]:
                errors.append(f"{name}: GPIO{g} is USB but is used for {sig}")
            if g in (45, 46) and str(g) not in b["pulldown_at_boot"]:
                errors.append(f"{name}: GPIO{g} ({sig}) is a strapping pin that must read 0 at reset; "
                              f"list it in pulldown_at_boot and give its load a pull-down")

        for sig in rules["adc1_required"]:
            if sig in where and where[sig] not in adc1:
                errors.append(f"{name}: {sig} on GPIO{where[sig]} must be an ADC1 pin (ADC2 stops working with WiFi)")
        for sig in rules["adc_required"]:
            if sig in where and where[sig] not in adc1 | adc2:
                errors.append(f"{name}: {sig} on GPIO{where[sig]} must be an ADC pin")

        ports = sorted(int(s[1:]) for s in where if re.fullmatch(r"S\d+", s))
        if ports != list(range(1, b["sensor_ports"] + 1)):
            errors.append(f"{name}: sensor ports are S{ports}, expected S1..S{b['sensor_ports']}")
        outs = sorted(int(s[3:]) for s in where if re.fullmatch(r"OUT\d+", s))
        if outs != list(range(1, b["relays"] + 1)):
            errors.append(f"{name}: relay outputs are OUT{outs}, expected OUT1..OUT{b['relays']}")

        b["_where"] = where

    boards = pm["boards"]
    for sig in rules["same_gpio_on_every_board"]:
        gpios = {n: b["_where"].get(sig) for n, b in boards.items()}
        if None in gpios.values() or len(set(gpios.values())) != 1:
            errors.append(f"{sig} must be on the same GPIO on every board, got {gpios}")
    # A port a smaller board has must sit on the same GPIO on the bigger one (SYS-01).
    for sig in {s for b in boards.values() for s in b["_where"] if re.fullmatch(r"(S|OUT)\d+", s)}:
        gpios = {b["_where"][sig] for b in boards.values() if sig in b["_where"]}
        if len(gpios) != 1:
            errors.append(f"{sig} is on different GPIOs on different boards: {sorted(gpios)}")

    # LoRa must keep the AWD1 field node's pin map, so the same driver serves both.
    design = os.path.normpath(os.path.join(HW, rules["lora_matches"]))
    with open(design) as f:
        awd = dict((sig, int(g)) for g, sig in re.findall(r'"IO(\d+)":\s*"(LORA_\w+)"', f.read()))
    if not awd:
        errors.append(f"could not read the LoRa pins from {design}")
    for n, b in boards.items():
        for sig, g in awd.items():
            if sig in b["_where"] and b["_where"][sig] != g:
                errors.append(f"{n}: {sig} is GPIO{b['_where'][sig]}, AWD1 uses GPIO{g}")
        lora = {s for s in b["_where"] if s.startswith("LORA_")}
        if lora and lora != set(awd):
            errors.append(f"{n}: LoRa pins {sorted(lora)} differ from AWD1's {sorted(awd)}")

    for n, b in boards.items():
        used = {int(g) for g in b["pins"]}
        print(f"{n}: {len(used)} of {len(module)} module GPIOs used, "
              f"free: {sorted(module - used) or 'none'}"
              + (f", plus {b['expander']['chip']} pins" if b["expander"] else ""))

    if errors:
        print(f"\n{len(errors)} problem(s):")
        for e in errors:
            print("  - " + e)
        sys.exit(1)
    print("Pin map OK.")


if __name__ == "__main__":
    main()
