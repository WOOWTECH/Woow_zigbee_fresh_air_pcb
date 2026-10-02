#!/usr/bin/env bash
# V2.0 PCB 一鍵重建：原 Gerber → V2.0 擺件與市電佈線 → Freerouting 低壓佈線 → 補線 → 鋪銅 → DRC
# 需求：KiCad 10（pcbnew python）、java 17、xvfb-run、Freerouting 1.9 jar、原始 Gerber（ORIG_GERBER，不公開）
# Freerouting 對輸入很敏感（板子小改，結果可能差很多），所以依序試幾組輪數，第一組「全部接通」的就採用。
set -euo pipefail
cd "$(dirname "$0")/.."
PCB=hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_pcb
PY=${PY_SCH:-python3}                          # 產生原理圖需要 kicad-sch-api
"$PY" scripts/gen_schematic.py 2.0
DESIGN_VERSION=2.0 python3 scripts/import_v13_pcb.py
cp "$PCB" "$PCB.pre_route"
best=999
for passes in ${FR_PASSES_LIST:-40 25 60 80}; do
  cp "$PCB.pre_route" "$PCB"
  echo "== Freerouting $passes 輪"
  FR_PASSES=$passes timeout 1200 python3 scripts/route_v20.py || { echo "  逾時或失敗，換下一組"; continue; }
  for net in $(python3 scripts/drc_summary.py --unconnected-nets 2>/dev/null); do
    python3 scripts/route_leftovers.py "$net" >/dev/null 2>&1 || echo "  補線失敗：$net"
  done
  python3 scripts/route_v20.py fill
  n=$(python3 scripts/drc_summary.py --unconnected-nets 2>/dev/null | wc -w)
  echo "  未連線網路：$n"
  if [ "$n" -lt "$best" ]; then best=$n; cp "$PCB" "$PCB.best"; fi
  [ "$n" -eq 0 ] && break
done
cp "$PCB.best" "$PCB"; rm -f "$PCB.pre_route" "$PCB.best"
python3 scripts/drc_summary.py -v
