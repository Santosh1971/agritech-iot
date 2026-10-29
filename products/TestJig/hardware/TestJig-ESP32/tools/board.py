"""Board geometry shared by the PCB scripts.

Local coordinates: mm from the board's top-left corner; KiCad coordinates add (OX, OY).
99 x 99 mm square board for the user's IP65 box: two M3 holes on the vertical centre line,
92 mm apart (the box's top-centre and bottom-centre bosses, same as WM1's hole size).
Inputs on the left wall, outputs on the right wall, DevKit USB out of the right wall, LoRa antenna at the top.
"""
import math

OX, OY = 50.0, 40.0
W, H = 99.0, 99.0
R_CORNER = 0.0
HOLES = [(49.5, 3.5), (49.5, 95.5)]
ANTENNA_KO = (28.6, 24.2, 41.2, 53.8)        # DevKit antenna end (left end of the module): no copper pour


def inside(x, y, margin=0.0):
    """True if local point (x, y) is inside the outline by at least `margin` mm."""
    if x < margin or y < margin or x > W - margin or y > H - margin:
        return False
    for cx in ((R_CORNER, W - R_CORNER) if R_CORNER > 0 else ()):
        if y < R_CORNER and ((x < R_CORNER and cx == R_CORNER) or (x > W - R_CORNER and cx != R_CORNER)):
            if math.hypot(x - cx, y - R_CORNER) > R_CORNER - margin:
                return False
    return True


def rect_inside(r, margin=0.0):
    """r = (x0, y0, x1, y1) in local mm."""
    return all(inside(x, y, margin) for x in (r[0], r[2]) for y in (r[1], r[3]))
