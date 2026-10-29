#!/bin/bash
# Full board rebuild: place -> Freerouting -> fix-up until DRC shows 0 unconnected -> exports.
# Usage: tools/run_all.sh            (from the project folder)
set -u
cd "$(dirname "$0")/.."
KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
quiet() { grep -v -E "swig|Fontconfig|stdpbase|Debug:|orphan GND"; }

$KPY -u tools/gen_pcb.py 2>&1 | quiet
$KPY -u tools/route.py 80 2>&1 | quiet
for i in 1 2 3 4; do
  $K pcb drc --schematic-parity --severity-all -o out/drc.rpt TestJig.kicad_pcb 2>&1 | grep -E "unconnected"
  n=$(grep -c "^\[unconnected_items" out/drc.rpt)
  [ "$n" = "0" ] && break
  $KPY -u tools/fixup.py 2>&1 | quiet
done
$KPY -u tools/widen.py 2>&1 | quiet
for i in 1 2 3; do   # widening can pinch the GND pour into islands: tie them back
  $K pcb drc --schematic-parity --severity-all -o out/drc.rpt TestJig.kicad_pcb 2>&1 | grep -E "unconnected"
  n=$(grep -c "^\[unconnected_items" out/drc.rpt)
  [ "$n" = "0" ] && break
  $KPY -u tools/fixup.py 2>&1 | quiet
done
$K pcb drc --schematic-parity --severity-all -o out/drc.rpt TestJig.kicad_pcb 2>&1 | grep -E "Found"
grep -E "^\[" out/drc.rpt | sed 's/:.*//' | sort | uniq -c | sort -rn
echo ALLDONE
