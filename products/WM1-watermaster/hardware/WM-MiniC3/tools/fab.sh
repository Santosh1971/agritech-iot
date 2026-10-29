#!/bin/zsh
# Fab outputs for WM-Mini C3 and its 99 x 98 two-up panel -> fab/
# Run from hardware/WM-MiniC3 after tools/build.sh (routed, DRC-clean board).
set -e
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
$KP tools/panel.py 2>&1 | grep -v "Fontconfig\|traits"
rm -rf fab && mkdir -p fab/renders
LAYERS=F.Cu,B.Cu,F.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts

gerbers() {   # $1 board file, $2 output name
  local d=fab/tmp-$2
  mkdir -p $d
  $K pcb export gerbers --layers $LAYERS --subtract-soldermask -o $d/ $1 >/dev/null 2>&1
  $K pcb export drill --format excellon --excellon-separate-th -o $d/ $1 >/dev/null 2>&1
  (cd $d && zip -q ../$2-gerbers.zip *)
  rm -rf $d
}
gerbers panel/WM-MiniC3-panel.kicad_pcb WM-MiniC3-panel      # ORDER THIS ONE
gerbers WM-MiniC3.kicad_pcb WM-MiniC3

P=WM-MiniC3
$K sch export pdf -o fab/$P-schematic.pdf $P.kicad_sch >/dev/null 2>&1
$K sch export bom --fields 'Reference,Value,Footprint,${QUANTITY}' --group-by 'Value,Footprint' -o fab/$P-bom-kicad.csv $P.kicad_sch >/dev/null 2>&1
$K pcb export pdf --layers F.Cu,F.SilkS,Edge.Cuts,F.Fab --mode-single -o fab/$P-print-1to1.pdf $P.kicad_pcb >/dev/null 2>&1
$K pcb export pdf --layers B.Cu,B.SilkS,Edge.Cuts --mode-single --mirror -o fab/$P-bottom-1to1.pdf $P.kicad_pcb >/dev/null 2>&1
$K pcb export step --force --subst-models -o fab/$P.step $P.kicad_pcb >/dev/null 2>&1 || true
$K pcb render --side top --width 1800 --height 1000 --quality high -o fab/renders/$P-top.png $P.kicad_pcb >/dev/null 2>&1
$K pcb render --side bottom --width 1800 --height 1000 --quality high -o fab/renders/$P-bottom.png $P.kicad_pcb >/dev/null 2>&1
$K pcb render --width 1800 --height 1200 --rotate ' -50,0,-20' --zoom 0.8 --quality high --background opaque -o fab/renders/$P-3d.png $P.kicad_pcb >/dev/null 2>&1
$K pcb render --side top --width 1200 --height 1200 --quality high -o fab/renders/$P-panel-top.png panel/WM-MiniC3-panel.kicad_pcb >/dev/null 2>&1
echo "fab/ written"; ls fab fab/renders
