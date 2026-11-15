"""Write the AWD.pretty sensing-pad footprints.

Pads are copper on F.Cu only - no F.Mask layer - so solder mask (and later the
epoxy/PU dip coat) covers them. Nothing in the water is ever bare copper.
"""
import os, uuid

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PRJ, "AWD.pretty")
os.makedirs(OUT, exist_ok=True)

TEMPLATE = '''(footprint "{name}"
	(version 20241229)
	(generator "pcbnew")
	(generator_version "9.0")
	(layer "F.Cu")
	(descr "{descr}")
	(tags "capacitive level sensor electrode AWD")
	(property "Reference" "REF**"
		(at 0 {ty} 0)
		(layer "F.Fab")
		(hide yes)
		(uuid "{u1}")
		(effects (font (size 0.8 0.8) (thickness 0.12)))
	)
	(property "Value" "{name}"
		(at 0 {vy} 0)
		(layer "F.Fab")
		(hide yes)
		(uuid "{u2}")
		(effects (font (size 0.8 0.8) (thickness 0.12)))
	)
	(attr smd exclude_from_pos_files exclude_from_bom)
	(fp_rect
		(start {x0} {y0})
		(end {x1} {y1})
		(stroke (width 0.1) (type solid))
		(fill no)
		(layer "F.Fab")
		(uuid "{u3}")
	)
	(pad "1" smd rect
		(at 0 0)
		(size {w} {h})
		(layers "F.Cu")
		(zone_connect 2)
		(uuid "{u4}")
	)
	(embedded_fonts no)
)
'''

def write(name, w, h, descr):
    u = [uuid.uuid4() for _ in range(4)]
    s = TEMPLATE.format(name=name, descr=descr, w=w, h=h, x0=-w / 2, y0=-h / 2, x1=w / 2, y1=h / 2,
                        ty=-h / 2 - 1, vy=h / 2 + 1, u1=u[0], u2=u[1], u3=u[2], u4=u[3])
    open(os.path.join(OUT, name + ".kicad_mod"), "w").write(s)

write("SensePad_6x18mm", 6, 18, "AWD level-strip sensing electrode, 6x18 mm on 20 mm pitch, mask-covered (v0.3, 2-layer)")
write("SensePad_6x12mm", 6, 12, "AWD level-strip dry reference electrode, 6x12 mm, mask-covered (v0.3)")
write("ReturnPad_12x20mm", 12, 20, "AWD level-strip GND water-return electrode, 12x20 mm, mask-covered (v0.3)")
print("footprints written to", OUT)
