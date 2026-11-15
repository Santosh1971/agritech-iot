#!/bin/zsh
# AWD field node v0.3 (2 layers): schematic -> board -> Freerouting -> GND stitching -> silkscreen -> ERC/DRC.
# Freerouting is not deterministic, so the board step is retried until DRC has no blocking issue.
# Run from hardware/AWD-FieldNode:  zsh tools/build.sh [tries]
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
KP=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
RPT=${TMPDIR:-/tmp}/awd-drc.rpt
python3 tools/gen_sch.py
python3 tools/gen_fp.py >/dev/null
$K sch erc --severity-all -o ${RPT:r}-erc.rpt AWD-FieldNode.kicad_sch >/dev/null 2>&1; grep "ERC messages" ${RPT:r}-erc.rpt
for i in $(seq 1 ${1:-5}); do
  (cd tools && $KP gen_pcb.py 2>&1 | grep "board written" \
     && ROUTE_PASSES=${ROUTE_PASSES:-60} $KP route.py 2>&1 | grep "^routed" \
     && $KP stitch.py 2>&1 | grep "stitching" && $KP silk.py 2>&1 | grep "references")
  $K pcb drc --schematic-parity --severity-all -o $RPT AWD-FieldNode.kicad_pcb >/dev/null 2>&1
  grep -E "^\*\* Found" $RPT
  errs=$(grep -cE "^\[(clearance|unconnected_items|copper_edge_clearance|starved_thermal|net_conflict|missing_footprint|invalid_outline|shorting_items|tracks_crossing|hole_clearance)\]" $RPT)
  echo "try $i: $errs blocking issues"
  [[ $errs == 0 ]] && break
done
grep -E "^\[" $RPT | sed 's/\].*/]/' | sort | uniq -c | sort -rn
