"""Generate the project-local symbol + footprint for the ESP32 30-pin DevKit."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from design import DEVKIT_PINS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

POWER = {'3V3': 'power_out', 'VIN': 'power_out', 'GND': 'power_in'}


def symbol():
    pins = []
    # Right side: pins 15 (top) .. 1 (bottom); left side: 16 (top) .. 30 (bottom) -- like the module top view.
    for n in range(1, 31):
        name = DEVKIT_PINS[n][0]
        etype = POWER.get(name, 'bidirectional')
        if n <= 15:
            x, y, ang = 15.24, -17.78 + (n - 1) * 2.54, 180
        else:
            x, y, ang = -15.24, 17.78 - (n - 16) * 2.54, 0
        pins.append('\t\t\t(pin %s line (at %.2f %.2f %d) (length 3.81) (name "%s" (effects (font (size 1.27 1.27)))) '
                    '(number "%d" (effects (font (size 1.27 1.27)))))' % (etype, x, y, ang, name, n))
    return '''(kicad_symbol_lib
	(version 20241209)
	(generator "testjig_gen")
	(generator_version "9.0")
	(symbol "ESP32_DevKit_30pin"
		(exclude_from_sim no) (in_bom yes) (on_board yes)
		(property "Reference" "U" (at 0 22.86 0) (effects (font (size 1.27 1.27))))
		(property "Value" "ESP32_DevKit_30pin" (at 0 -22.86 0) (effects (font (size 1.27 1.27))))
		(property "Footprint" "TestJig:ESP32_DevKit_30pin_W25.4mm" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(property "Description" "ESP32-WROOM-32 DevKit V1, 30 pins (DOIT style), plugged in 2x15 female headers" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(symbol "ESP32_DevKit_30pin_0_1"
			(rectangle (start -11.43 20.32) (end 11.43 -20.32) (stroke (width 0.254) (type default)) (fill (type background)))
		)
		(symbol "ESP32_DevKit_30pin_1_1"
%s
		)
	)
)
''' % '\n'.join(pins)


def footprint(row=25.4):
    """Pin 1 (3V3) at origin; pins 1..15 go up (-y) in the right column, 16..30 in the left column.
    USB end of the module at +y, antenna end at -y (same orientation as WM1)."""
    pads = []
    for n in range(1, 31):
        if n <= 15:
            x, y = 0.0, -(n - 1) * 2.54
        else:
            x, y = -row, -(30 - n) * 2.54
        shape = 'rect' if n == 1 else 'oval'
        pads.append('\t(pad "%d" thru_hole %s (at %.3f %.3f) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))'
                    % (n, shape, x, y))
    x0, x1 = -row - 1.5, 1.5
    y0, y1 = -47.0, 5.5
    cx = -row / 2
    return '''(footprint "ESP32_DevKit_30pin_W25.4mm"
	(version 20241229)
	(generator "testjig_gen")
	(layer "F.Cu")
	(descr "ESP32 DevKit V1 30-pin (DOIT) on 2x15 female headers, rows %.2f mm apart. Courtyard = header rows only: low SMD parts may sit under the socketed module. VERIFY row spacing against your module.")
	(property "Reference" "REF**" (at %.2f -49 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
	(property "Value" "ESP32_DevKit_30pin" (at %.2f -20 90) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
	(attr through_hole)
	(fp_rect (start %.2f %.2f) (end %.2f %.2f) (stroke (width 0.15) (type default)) (fill no) (layer "F.SilkS"))
	(fp_rect (start -1.5 -37.06) (end 1.5 1.5) (stroke (width 0.05) (type default)) (fill no) (layer "F.CrtYd"))
	(fp_rect (start %.2f -37.06) (end %.2f 1.5) (stroke (width 0.05) (type default)) (fill no) (layer "F.CrtYd"))
	(fp_rect (start %.2f -47) (end %.2f -39) (stroke (width 0.1) (type dash)) (fill no) (layer "F.SilkS"))
	(fp_text user "ANTENNA" (at %.2f -43) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
	(fp_text user "3V3" (at 2.2 0 90) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))
	(fp_text user "VIN" (at %.2f 0 90) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))
%s
)
''' % (row, cx, cx, x0, y0, x1, y1, -row - 1.5, -row + 1.5, x0, x1, cx, -row - 2.2,
       '\n'.join(pads))


if __name__ == '__main__':
    open(os.path.join(ROOT, 'TestJig.kicad_sym'), 'w').write(symbol())
    open(os.path.join(ROOT, 'TestJig.pretty', 'ESP32_DevKit_30pin_W25.4mm.kicad_mod'), 'w').write(footprint())
    open(os.path.join(ROOT, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n\t(version 7)\n\t(lib (name "TestJig")(type "KiCad")(uri "${KIPRJMOD}/TestJig.kicad_sym")(options "")(descr "Test jig parts"))\n)\n')
    open(os.path.join(ROOT, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n\t(version 7)\n\t(lib (name "TestJig")(type "KiCad")(uri "${KIPRJMOD}/TestJig.pretty")(options "")(descr "Test jig footprints"))\n)\n')
    print('lib ok')
