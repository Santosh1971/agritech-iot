"""Attach the ASC.3dshapes STEP models to footprints that have none (our own parts and the
MetzConnect terminals, whose models this KiCad install lacks). Edits the routed boards in place,
so no re-route is needed. gen_pcb.py calls set_models() too, so regenerated boards keep them.

    $KP tools/apply_3d.py            # ASC-MiniC3.kicad_pcb and ASC-AHT20.kicad_pcb
"""
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.dirname(HERE)
SHAPES = os.path.join(PRJ, "ASC.3dshapes")

def set_models(board, prefix="${KIPRJMOD}/ASC.3dshapes/"):
    n = 0
    for fp in board.GetFootprints():
        name = str(fp.GetFPID().GetLibItemName())
        if not os.path.exists(os.path.join(SHAPES, name + ".step")):
            continue
        fp.Models().clear()
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = prefix + name + ".wrl"     # the viewer shows .wrl; STEP export swaps in the .step (--subst-models)
        fp.Models().append(m)
        n += 1
    return n

if __name__ == "__main__":
    for p in ("ASC-MiniC3", "ASC-AHT20"):
        path = os.path.join(PRJ, p + ".kicad_pcb")
        b = pcbnew.LoadBoard(path)
        n = set_models(b)
        pcbnew.SaveBoard(path, b)
        print("%s: %d footprints given ASC models" % (p, n))
