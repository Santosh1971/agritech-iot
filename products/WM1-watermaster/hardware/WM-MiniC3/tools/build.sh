#!/bin/zsh
# Schematic -> placed board -> routed board -> DRC, retried until Freerouting gives a clean result.
# Run from hardware/WM-MiniC3:  zsh tools/build.sh [tries]
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
RPT=${TMPDIR:-/tmp}/wm-minic3-drc.rpt
python3 tools/gen_sch.py
$K sch erc --severity-all -o ${RPT:r}-erc.rpt WM-MiniC3.kicad_sch >/dev/null 2>&1; grep "ERC messages" ${RPT:r}-erc.rpt
for i in $(seq 1 ${1:-4}); do
  $KP tools/gen_pcb.py 2>&1 | grep -v "Fontconfig\|traits"
  $KP tools/route.py 2>&1 | grep "^routed"
  $K pcb drc --schematic-parity --severity-all -o $RPT WM-MiniC3.kicad_pcb >/dev/null 2>&1
  grep -E "^\*\* Found" $RPT
  errs=$(grep -cE "^\[(clearance|unconnected_items|copper_edge_clearance|starved_thermal|net_conflict|missing_footprint|invalid_outline|shorting_items|tracks_crossing|hole_clearance)\]" $RPT)
  echo "try $i: $errs blocking issues"
  [[ $errs == 0 ]] && break
done
grep -E "^\[" $RPT | sort | uniq -c | sort -rn
