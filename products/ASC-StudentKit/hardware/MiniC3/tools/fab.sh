#!/bin/zsh
# Fab outputs for the Mini-C3, the AHT20 stick and the 96 x 100 panel -> fab/
# Run from hardware/MiniC3 after gen_pcb.py / route.py (both boards) and panel.py.
set -e
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
rm -rf fab && mkdir -p fab/renders
LAYERS=F.Cu,B.Cu,F.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts

gerbers() {   # $1 board file, $2 output name, $3 extra layers
  local d=fab/tmp-$2
  mkdir -p $d
  $K pcb export gerbers --layers $LAYERS$3 --subtract-soldermask -o $d/ $1 >/dev/null
  $K pcb export drill --format excellon --excellon-separate-th -o $d/ $1 >/dev/null
  (cd $d && zip -q ../$2-gerbers.zip *)
  rm -rf $d
}
gerbers panel/ASC-MiniC3-panel.kicad_pcb ASC-MiniC3-panel ,Cmts.User      # ORDER THIS ONE
gerbers ASC-MiniC3.kicad_pcb ASC-MiniC3 ""
gerbers ASC-AHT20.kicad_pcb ASC-AHT20 ""

for p in ASC-MiniC3 ASC-AHT20; do
  $K sch export pdf -o fab/$p-schematic.pdf $p.kicad_sch >/dev/null
  $K sch export bom --fields 'Reference,Value,Footprint,${QUANTITY}' --group-by 'Value,Footprint' -o fab/$p-bom-kicad.csv $p.kicad_sch >/dev/null
  $K pcb export pdf --layers F.Cu,F.SilkS,Edge.Cuts,F.Fab --mode-single -o fab/$p-print-1to1.pdf $p.kicad_pcb >/dev/null
  $K pcb export pos --side front --format csv --units mm --smd-only -o fab/$p-pos.csv $p.kicad_pcb >/dev/null
  $K pcb render --side top --width 1200 --height 1200 --quality high -o fab/renders/$p-top.png $p.kicad_pcb >/dev/null
  $K pcb render --side bottom --width 1200 --height 1200 --quality high -o fab/renders/$p-bottom.png $p.kicad_pcb >/dev/null
done
$K pcb export pos --side front --format csv --units mm --smd-only -o fab/ASC-MiniC3-panel-pos.csv panel/ASC-MiniC3-panel.kicad_pcb >/dev/null
$K pcb render --side top --width 1200 --height 1250 --quality high -o fab/renders/ASC-MiniC3-panel-top.png panel/ASC-MiniC3-panel.kicad_pcb >/dev/null
python3 tools/jlc_files.py
echo "fab/ written"
