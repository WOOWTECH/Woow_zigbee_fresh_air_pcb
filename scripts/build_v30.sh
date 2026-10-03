#!/usr/bin/env bash
# V3.0 PCB 一鍵重建：git 標籤 v2.0 的 PCB（市電佈線沿用）→ V3.0 換件擺件 → Freerouting 低壓佈線 → 補線 → 鋪銅 → 縫合過孔 → 3D 模型 → DRC
# 需求：KiCad 10（pcbnew python）、java 17、xvfb-run、Freerouting 1.9 jar、kicad-sch-api（不需原始 Gerber）
# Freerouting 對輸入很敏感（板子小改，結果可能差很多），所以依序試幾組輪數，第一組「全部接通」的就採用。
set -euo pipefail
cd "$(dirname "$0")/.."
PCB=hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_pcb
PY=${PY_SCH:-python3}                          # 產生原理圖需要 kicad-sch-api
"$PY" scripts/gen_schematic.py 3.0
python3 scripts/upgrade_v30_pcb.py
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
# GND 縫合過孔（3mm 格點＋訊號過孔旁），再清掉孔距過近的
python3 scripts/route_v20.py fill
python3 scripts/add_stitching.py 3.0
python3 scripts/add_stitching.py --near-signal
python3 scripts/route_v20.py fill
python3 scripts/add_stitching.py --prune
python3 scripts/route_v20.py fill
# Freerouting 不知道板邊 0.5mm 規則：刪掉貼邊的走線，鋪銅接不回的網路再用格點佈線補
for net in $(python3 scripts/fix_edge_tracks.py 2>/dev/null | tail -1); do
  python3 scripts/route_v20.py fill
  python3 scripts/drc_summary.py --unconnected-nets 2>/dev/null | grep -qw -- "$net" && python3 scripts/route_leftovers.py "$net" >/dev/null 2>&1
done
python3 scripts/route_v20.py fill
# 3D 模型路徑（封裝重新產生後會被清掉）
python3 scripts/assign_3d.py
python3 scripts/drc_summary.py -v
