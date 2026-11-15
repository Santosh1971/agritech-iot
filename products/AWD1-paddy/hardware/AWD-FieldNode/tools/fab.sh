#!/bin/zsh
# Fab outputs for the AWD field node v0.3 (2 layers) -> fab/
# Run from hardware/AWD-FieldNode after tools/build.sh (routed, DRC-clean board).
set -e
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
P=AWD-FieldNode
rm -rf fab && mkdir -p fab/gerber fab/renders
$K pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
   --subtract-soldermask -o fab/gerber/ $P.kicad_pcb >/dev/null 2>&1
$K pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 -o fab/gerber/ $P.kicad_pcb >/dev/null 2>&1
(cd fab/gerber && zip -q ../$P-gerbers.zip *)
$K sch export pdf -o fab/$P-schematic.pdf $P.kicad_sch >/dev/null 2>&1
cp fab/$P-schematic.pdf $P-schematic.pdf
$K sch export bom --fields 'Reference,Value,Footprint,${QUANTITY}' --group-by 'Value,Footprint' \
   --exclude-dnp -o fab/$P-bom.csv $P.kicad_sch >/dev/null 2>&1
$K pcb export pos --format csv --units mm --exclude-dnp -o fab/$P-pos.csv $P.kicad_pcb >/dev/null 2>&1
$K pcb export pdf --layers F.Cu,F.SilkS,Edge.Cuts,F.Fab --mode-single -o fab/$P-top-1to1.pdf $P.kicad_pcb >/dev/null 2>&1
$K pcb export pdf --layers B.Cu,B.SilkS,Edge.Cuts,B.Fab --mode-single --mirror -o fab/$P-bottom-1to1.pdf $P.kicad_pcb >/dev/null 2>&1
$K pcb export step --force --subst-models -o fab/$P.step $P.kicad_pcb >/dev/null 2>&1 || true
for side in top bottom; do
  $K pcb render --side $side --width 1200 --height 2400 --quality high --background opaque -o fab/renders/$P-$side.png $P.kicad_pcb >/dev/null 2>&1
done
# head close-ups: crop the head out of a tall high-res render
for side in top bottom; do
  $K pcb render --side $side --width 2000 --height 4000 --quality high --background opaque -o fab/renders/tmp-$side.png $P.kicad_pcb >/dev/null 2>&1
  python3 - fab/renders/tmp-$side.png fab/renders/$P-head-$side.png <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
px = im.load()
bg = px[5, 5]
rows = [y for y in range(h) if any(sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 60 for x in range(0, w, 8))]
top = rows[0]
cols = [x for x in range(w) if any(sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 60 for y in range(top, top + 60, 4))]
x0, x1 = cols[0], cols[-1]
head_h = int((x1 - x0) * 60 / 40)          # head is 40 x 60 mm
pad = int((x1 - x0) * 0.05)
im.crop((x0 - pad, top - pad, x1 + pad, top + head_h + pad)).save(sys.argv[2])
PY
  rm fab/renders/tmp-$side.png
done
$K pcb render --width 2000 --height 1200 --rotate ' -55,0,-30' --zoom 1.2 --quality high --background opaque -o fab/renders/$P-3d.png $P.kicad_pcb >/dev/null 2>&1
echo "fab/ written"; ls fab fab/renders
