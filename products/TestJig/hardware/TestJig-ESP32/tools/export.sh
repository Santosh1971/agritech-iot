#!/bin/bash
# Re-export review + fab outputs from the current schematic/PCB.
set -u
cd "$(dirname "$0")/.."
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
TMP=${TMPDIR:-/tmp}
cp TestJig.kicad_pcb out/routed-good.kicad_pcb
rm -rf out/fab/gerber out/fab/TestJig-gerbers.zip && mkdir -p out/fab/gerber
$K sch export pdf -o out/TestJig-schematic.pdf TestJig.kicad_sch >/dev/null 2>&1
$K sch export bom --fields 'Reference,Value,Footprint,${QUANTITY}' --group-by Value,Footprint --exclude-dnp -o out/fab/TestJig-BOM.csv TestJig.kicad_sch >/dev/null 2>&1
$K pcb export pdf --layers F.Cu,F.SilkS,Edge.Cuts --mode-single -o out/TestJig-pcb-top.pdf TestJig.kicad_pcb >/dev/null 2>&1
$K pcb export pdf --layers B.Cu,Edge.Cuts --mode-single -o out/TestJig-pcb-bottom.pdf TestJig.kicad_pcb >/dev/null 2>&1
$K pcb export gerbers --layers F.Cu,B.Cu,F.SilkS,B.SilkS,F.Mask,B.Mask,F.Paste,Edge.Cuts -o out/fab/gerber/ TestJig.kicad_pcb >/dev/null 2>&1
$K pcb export drill --format excellon --excellon-separate-th -o out/fab/gerber/ TestJig.kicad_pcb >/dev/null 2>&1
$K pcb export pos --format csv --units mm --side front -o out/fab/TestJig-pos-top.csv TestJig.kicad_pcb >/dev/null 2>&1
(cd out/fab && zip -qr TestJig-gerbers.zip gerber)
python3 tools/gen_bom.py
$K pcb render --side top --quality high --width 2000 --height 2000 -o out/TestJig-3D-top.png TestJig.kicad_pcb >/dev/null 2>&1
$K pcb render --side bottom --quality basic --width 1600 --height 1600 -o out/TestJig-3D-bottom.png TestJig.kicad_pcb >/dev/null 2>&1
for spec in "top-copper:F.Cu,F.SilkS,Edge.Cuts" "bottom-copper:B.Cu,Edge.Cuts"; do
  f=${spec%%:*}; l=${spec#*:}
  $K pcb export svg --layers $l --page-size-mode 2 --exclude-drawing-sheet -o "$TMP/$f.svg" TestJig.kicad_pcb >/dev/null 2>&1
  (cd "$TMP" && rm -f $f.svg.png && qlmanage -t -s 2400 -o . $f.svg >/dev/null 2>&1)
  mv "$TMP/$f.svg.png" out/$f.png
done
echo exported
