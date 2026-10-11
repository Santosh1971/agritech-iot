"""Simple coloured STEP models for the parts KiCad has no 3D model for -> ASC.3dshapes/.

Each model is a few extruded boxes / cylinders, written as plain B-rep solids (planar faces,
shared edges) so KiCad's 3D viewer and any STEP reader mesh them, sized from the datasheets / footprints. Good enough for the 3D view,
enclosure fit checks and a STEP export of the board; not for tooling.

    python3 tools/gen_3d.py

Coordinates below are footprint coordinates (x right, y DOWN, z up from the board top, mm);
the writer flips y because KiCad's 3D model space has y up.
"""
import math, os

PRJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PRJ, "ASC.3dshapes")
os.makedirs(OUT, exist_ok=True)

BLACK, DARK, SILVER, WHITE = (0.08, 0.08, 0.08), (0.12, 0.12, 0.14), (0.78, 0.78, 0.80), (0.94, 0.94, 0.92)
GREEN, BLUE, BOARD, GOLD = (0.10, 0.50, 0.22), (0.10, 0.30, 0.70), (0.08, 0.12, 0.30), (0.85, 0.70, 0.25)

# ---- geometry: a solid is (faces, colour); a face is a list of 3D points, counter-clockwise seen from outside
def prism(poly, z0, z1):
    """Extrude a polygon (footprint x, y) from z0 to z1; faces come out with outward normals."""
    pts = [(x, -y) for x, y in poly]                      # to model space (y up)
    area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    if area < 0:
        pts = pts[::-1]                                   # make the loop counter-clockwise seen from +z
    bot = [(x, y, z0) for x, y in pts][::-1]
    top = [(x, y, z1) for x, y in pts]
    sides = []
    n = len(pts)
    for i in range(n):
        (xa, ya), (xb, yb) = pts[i], pts[(i + 1) % n]
        sides.append([(xa, ya, z0), (xb, yb, z0), (xb, yb, z1), (xa, ya, z1)])
    return [bot, top] + sides

def box(x0, y0, x1, y1, z0, z1):
    return prism([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)

def cyl(cx, cy, r, z0, z1, n=24):
    return prism([(cx + r * math.cos(2 * math.pi * i / n), cy - r * math.sin(2 * math.pi * i / n)) for i in range(n)], z0, z1)

def disc_y(cx, cz, r, y0, y1, n=32):
    """Cylinder with its axis along footprint y (a coin standing up), from footprint y0 to y1."""
    faces = []
    ring = [(cx + r * math.cos(2 * math.pi * i / n), cz + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
    Y0, Y1 = -y0, -y1                              # model space
    a = [(x, Y0, z) for x, z in ring]
    b = [(x, Y1, z) for x, z in ring]
    # outward normals: face at Y0 points +Y (Y0 > Y1), face at Y1 points -Y
    faces.append(a[::-1] if Y0 < Y1 else a)
    faces.append(b if Y0 < Y1 else b[::-1])
    for i in range(n):
        j = (i + 1) % n
        q = [a[i], a[j], b[j], b[i]]
        faces.append(q if Y0 > Y1 else q[::-1])
    return faces

# ---- STEP writer (AP214, faceted B-rep, colours through styled items)
def write_step(name, solids):
    ents = []
    def e(s):
        ents.append(s)
        return "#%d" % len(ents)
    ctx_app = e("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
    e("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,%s)" % ctx_app)
    pctx = e("PRODUCT_CONTEXT('',%s,'mechanical')" % ctx_app)
    prod = e("PRODUCT('%s','%s','',(%s))" % (name, name, pctx))
    e("PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(%s))" % prod)
    pdf = e("PRODUCT_DEFINITION_FORMATION('','',%s)" % prod)
    pdc = e("PRODUCT_DEFINITION_CONTEXT('part definition',%s,'design')" % ctx_app)
    pd = e("PRODUCT_DEFINITION('design','',%s,%s)" % (pdf, pdc))
    pds = e("PRODUCT_DEFINITION_SHAPE('','',%s)" % pd)
    lu = e("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    au = e("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    su = e("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    unc = e("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-05),%s,'distance_accuracy_value','confusion accuracy')" % lu)
    gctx = e("( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((%s)) "
             "GLOBAL_UNIT_ASSIGNED_CONTEXT((%s,%s,%s)) REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY') )" % (unc, lu, au, su))
    def pt(p):
        return e("CARTESIAN_POINT('',(%.4f,%.4f,%.4f))" % p)
    def dr(v):
        return e("DIRECTION('',(%.6f,%.6f,%.6f))" % v)
    o = pt((0, 0, 0)); ax = e("AXIS2_PLACEMENT_3D('',%s,%s,%s)" % (o, dr((0, 0, 1)), dr((1, 0, 0))))
    breps, styled = [], []
    for k, (faces, col) in enumerate(solids):
        vid, eid, fids = {}, {}, []
        def vert(p):
            key = tuple(round(c, 5) for c in p)
            if key not in vid:
                vid[key] = e("VERTEX_POINT('',%s)" % pt(key))
            return key, vid[key]
        def edge(pa, pb):
            (ka, va), (kb, vb) = vert(pa), vert(pb)
            if (kb, ka) in eid:
                return "ORIENTED_EDGE('',*,*,%s,.F.)" % eid[(kb, ka)]
            if (ka, kb) not in eid:
                d = [kb[i] - ka[i] for i in range(3)]
                ln = math.sqrt(sum(c * c for c in d)) or 1.0
                line = e("LINE('',%s,VECTOR('',%s,%.5f))" % (pt(ka), dr(tuple(c / ln for c in d)), ln))
                eid[(ka, kb)] = e("EDGE_CURVE('',%s,%s,%s,.T.)" % (va, vb, line))
            return "ORIENTED_EDGE('',*,*,%s,.T.)" % eid[(ka, kb)]
        for f in faces:
            nx = ny = nz = 0.0
            for i in range(len(f)):
                p, q = f[i], f[(i + 1) % len(f)]
                nx += (p[1] - q[1]) * (p[2] + q[2]); ny += (p[2] - q[2]) * (p[0] + q[0]); nz += (p[0] - q[0]) * (p[1] + q[1])
            ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            nrm = (nx / ln, ny / ln, nz / ln)
            u = [f[1][i] - f[0][i] for i in range(3)]
            ul = math.sqrt(sum(c * c for c in u)) or 1.0
            ref = [c / ul for c in u]
            dot = sum(ref[i] * nrm[i] for i in range(3))            # make ref exactly perpendicular to the normal
            ref = [ref[i] - dot * nrm[i] for i in range(3)]
            rl = math.sqrt(sum(c * c for c in ref)) or 1.0
            ref = tuple(c / rl for c in ref)
            plane = e("PLANE('',AXIS2_PLACEMENT_3D('',%s,%s,%s))" % (pt(f[0]), dr(nrm), dr(ref)))
            oes = [e(edge(f[i], f[(i + 1) % len(f)])) for i in range(len(f))]
            loop = e("EDGE_LOOP('',(%s))" % ",".join(oes))
            bound = e("FACE_OUTER_BOUND('',%s,.T.)" % loop)
            fids.append(e("ADVANCED_FACE('',(%s),%s,.T.)" % (bound, plane)))
        shell = e("CLOSED_SHELL('',(%s))" % ",".join(fids))
        brep = e("MANIFOLD_SOLID_BREP('solid%d',%s)" % (k, shell))
        breps.append(brep)
        rgb = e("COLOUR_RGB('',%.3f,%.3f,%.3f)" % col)
        fill = e("FILL_AREA_STYLE('',(FILL_AREA_STYLE_COLOUR('',%s)))" % rgb)
        sstyle = e("SURFACE_STYLE_USAGE(.BOTH.,SURFACE_SIDE_STYLE('',(SURFACE_STYLE_FILL_AREA(%s))))" % fill)
        styled.append(e("STYLED_ITEM('color',(PRESENTATION_STYLE_ASSIGNMENT((%s))),%s)" % (sstyle, brep)))
    rep = e("ADVANCED_BREP_SHAPE_REPRESENTATION('%s',(%s,%s),%s)" % (name, ax, ",".join(breps), gctx))
    e("SHAPE_DEFINITION_REPRESENTATION(%s,%s)" % (pds, rep))
    e("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION('',(%s),%s)" % (",".join(styled), gctx))
    body = "\n".join("#%d=%s;" % (i + 1, s) for i, s in enumerate(ents))
    txt = ("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ASC simple model'),'2;1');\n"
           "FILE_NAME('%s.step','2026-10-10T00:00:00',('Agri Sensors and Controls'),(''),'asc gen_3d.py','asc gen_3d.py','');\n"
           "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n%s\nENDSEC;\nEND-ISO-10303-21;\n") % (name, body)
    open(os.path.join(OUT, name + ".step"), "w").write(txt)

# ---- the models
def supermini():
    """ESP32-C3 Super Mini on two 1x8 sockets. Dimensions from the maker's drawing (nologo.tech):
    board 18 x 22.52 mm, rows 15.24 mm apart; first pin 1.53 mm from the USB end, last pin 3.18 mm
    from the antenna end; USB-C overhangs the USB end by about 1.9 mm."""
    top, bot = -8.89 - 1.53, 8.89 + 3.18
    s = []
    for x in (-7.62, 7.62):
        s.append((box(x - 1.27, -10.16, x + 1.27, 10.16, 0, 8.5), BLACK))          # female socket strip
        s.append((box(x - 1.27, -10.16, x + 1.27, 10.16, 8.5, 11.0), DARK))        # module's male header plastic
    s.append((box(-9, top, 9, bot, 11.0, 12.0), BLACK))                            # Super Mini PCB (black)
    s.append((box(-4.47, top - 1.9, 4.47, top - 1.9 + 7.35, 12.0, 15.26), SILVER)) # USB-C receptacle
    for x in (-3.0, 3.0):
        s.append((box(x - 1.6, -1.0, x + 1.6, 1.8, 12.0, 13.4), SILVER))           # BOOT / RST buttons
    s.append((box(-1.8, 3.0, 1.8, 6.6, 12.0, 12.9), DARK))                         # ESP32-C3 QFN32 5x5 (rotated on the board)
    s.append((box(-3.0, bot - 2.4, 3.0, bot - 0.4, 12.0, 13.0), (0.85, 0.45, 0.45)))  # ceramic antenna "C3"
    return s

def cr2032():
    return [(disc_y(0, 11.0, 10.0, 0.4, 3.6), SILVER),
            (box(-8.0, -0.15, -7.2, 0.15, -1.6, 1.0), SILVER), (box(7.2, -0.15, 8.0, 0.15, -1.6, 1.0), SILVER),
            (box(-0.4, 3.85, 0.4, 4.15, -1.6, 1.0), SILVER)]

def hf46f():
    return [(box(-1.0, -5.95, 19.5, 1.25, 0, 15.3), BLACK)]

def terminal(n, pitch, color, h, y0, y1, screw_r):
    x0, x1 = -pitch / 2 - 0.2, (n - 1) * pitch + pitch / 2 + 0.2
    s = [(box(x0, y0, x1, y1, 0, h), color)]
    for i in range(n):
        x = i * pitch
        s.append((box(x - 1.1, y0 - 0.05, x + 1.1, y0 + 0.6, 1.5, 4.6), BLACK))       # wire entry (on the -y side)
        s.append((cyl(x, (y0 + y1) / 2 + 0.6, screw_r, h, h + 0.3), SILVER))          # screw head
    return s

def aht20():
    return [(box(-1.5, -1.5, 1.5, 1.5, 0, 1.0), (0.85, 0.85, 0.82)), (box(-0.6, -0.6, 0.6, 0.6, 1.0, 1.05), BLACK)]

def standoff():
    return [(cyl(0, 0, 4.5, -6.6, -6.0), WHITE), (cyl(0, 0, 2.6, -6.0, -1.6), WHITE), (cyl(0, 0, 2.2, 0, 2.6), WHITE)]

MODELS = {
    "ESP32-C3_SuperMini_Socket": supermini(),
    "Battery_CR2032_Vertical_3pin": cr2032(),
    "Relay_SPST_Hongfa_HF46F": hf46f(),
    "Aosong_AHT20_DFN-6_3x3mm": aht20(),
    "Snap_Standoff_4.2mm": standoff(),
    "TerminalBlock_MetzConnect_Type086_RT03402HBLC_1x02_P3.81mm_Horizontal": terminal(2, 3.81, GREEN, 8.6, -3.8, 3.7, 1.3),
    "TerminalBlock_MetzConnect_Type086_RT03404HBLC_1x04_P3.81mm_Horizontal": terminal(4, 3.81, GREEN, 8.6, -3.8, 3.7, 1.3),
    "TerminalBlock_MetzConnect_Type086_RT03405HBLC_1x05_P3.81mm_Horizontal": terminal(5, 3.81, GREEN, 8.6, -3.8, 3.7, 1.3),
    "TerminalBlock_MetzConnect_Type101_RT01602HBWC_1x02_P5.08mm_Horizontal": terminal(2, 5.08, BLUE, 10.0, -4.0, 4.0, 1.6),
}

def write_wrl(name, solids):
    """VRML 2 copy of the model: KiCad's 3D viewer always displays these (1 unit = 0.1 inch)."""
    out = ["#VRML V2.0 utf8", "# %s, ASC gen_3d.py" % name]
    for faces, col in solids:
        pts, idx = [], []
        for f in faces:
            loop = []
            for p in f:
                loop.append(len(pts))
                pts.append(p)
            idx.append(loop)
        out.append("Shape { appearance Appearance { material Material { diffuseColor %.3f %.3f %.3f "
                   "specularColor 0.2 0.2 0.2 shininess 0.3 } }" % col)
        out.append(" geometry IndexedFaceSet { solid FALSE creaseAngle 0.5")
        out.append("  coord Coordinate { point [ %s ] }" % ", ".join("%.4f %.4f %.4f" % tuple(c / 2.54 for c in p) for p in pts))
        out.append("  coordIndex [ %s ] } }" % ", ".join(" ".join(str(i) for i in loop) + " -1" for loop in idx))
    open(os.path.join(OUT, name + ".wrl"), "w").write("\n".join(out) + "\n")

if __name__ == "__main__":
    for name, solids in MODELS.items():
        write_step(name, solids)
        write_wrl(name, solids)
    print("%d models -> %s" % (len(MODELS), OUT))
