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
# head close-ups (zoom on the top 70 mm)
$K pcb render --side top --width 1600 --height 1600 --zoom 4.5 --pan '0,105,0' --quality high --background opaque -o fab/renders/$P-head-top.png $P.kicad_pcb >/dev/null 2>&1 || true
$K pcb render --side bottom --width 1600 --height 1600 --zoom 4.5 --pan '0,105,0' --quality high --background opaque -o fab/renders/$P-head-bottom.png $P.kicad_pcb >/dev/null 2>&1 || true
$K pcb render --width 2000 --height 1200 --rotate ' -55,0,-30' --zoom 1.2 --quality high --background opaque -o fab/renders/$P-3d.png $P.kicad_pcb >/dev/null 2>&1
echo "fab/ written"; ls fab fab/renders
