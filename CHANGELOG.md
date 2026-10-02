# 變更紀錄

格式參考 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，版號用 `硬體大版.小版`。

## [2.0.1] — 設計驗證

- 完整驗證報告 `docs/verification/`：ERC/DRC/parity、SPICE（ngspice）、PCB 計算器（IPC-2221）、熱分析、EMC 預檢、3D／STEP、kicadiff、Jobset。
- PCB：新增 GND 縫合過孔（3mm 格點 88 顆＋訊號過孔旁 3 顆，移除孔距過近 1 顆）。DRC 仍為 0。
- 自訂零件 3D 模型（`hardware/lib/WOOW.3dshapes/`，`scripts/make_3d_models.py`）。
- `.kicad_jobset`：KiCad 原生一鍵產出，8/8 成功。
- `.kicad_pro` MAINS 類別間距 2.5mm → 1.5mm，與 `.kicad_dru` 一致。
- CI：新增 SPICE 模擬（繼電器關斷尖峰 < 20V 才通過）與 STEP 輸出。
- 新增標籤 `v1.3`（含 V1.3 PCB；`v1.3-baseline` 只有原理圖）。
- **發現**：市電 1.5mm 走線在 1oz 銅剛好等於保險絲 3.15A，負載 >3A 須改 2oz 銅。

## [2.0] — 安規改版

**檢查結果**：KiCad ERC 0 錯誤；DRC 0 錯誤、0 未連線、原理圖與 PCB 一致（含市電安規規則）。

### 電路
- 新增 F1 慢斷保險絲 T3.15A（AC_L 入口，同時保護 IRM 電源與 P3 輸出到風扇的迴路）。
- 新增 RV1 壓敏電阻 07D471K（L-N 突波）。
- U4：LM1117 線性穩壓 → AP63203WU 同步降壓（L2 3.9µH、C7/C13 22µF、C12 BST 100nF，依規格書 Table 2）。12V 側預算 ~146mA，低於 IRM-02-12 額定 167mA。
- ZS3L 串口接到 STM32 USART1（TXD→PA10、RXD←PA9）、RST←PB1（R13 10k 上拉）；指示燈改由 PB0 控制。
- NRST 電容 1µF → 100nF；VDDA 新增 C10 1µF；BOOT0 新增測試點 TP1。
- IRM 的 AC 兩腳 L/N 對調（佈線需要，IRM 輸入不分極性）。

### PCB
- 市電 ↔ 低壓：全板 ≥ 6.4mm（原 0.127mm）。GND 鋪銅由 DRC 規則自動退讓。
- 市電不同網路之間 ≥ 1.5mm（IEC 60664-1 功能絕緣；G5Q 相鄰接點腳本身約 1.67mm）。
- 銅到板邊 ≥ 0.5mm（原 0.26mm）。
- 固定孔到市電銅箔 ≥ 2.3mm（新 DRC 規則 `hole_to_mains`）：右緣市電走道外移到 x=30.2mm、AC_N 在 H2 旁內彎；H2 0.85→2.85mm、H4 1.90→2.90mm。仍須用尼龍螺絲。
- 繼電器驅動元件（D/Q/R）移到線圈腳旁、離市電 6.4mm 以外；RF1、P2、S1（改背面）、降壓電路重新擺放。
- 新增固定孔 H1–H4（M3，非金屬化，位置不變）。
- 繼電器改用收緊 courtyard 的 `WOOW:Relay_SPDT_Omron-G5Q-1_Tight`（原板間距 10.67mm、本體 10.0mm）。
- LCSC 料號不印在絲印上。

### 工具
- `scripts/build_v20.sh` 一鍵重建：原 Gerber → V2.0 擺件 → 市電格點佈線（`mains_router.py`）→ Freerouting 低壓佈線（`route_v20.py`）→ 補線（`route_leftovers.py`）→ 鋪銅 → DRC。
- `.github/workflows/kicad-ci.yml`：每次 push / PR 跑 ERC、DRC、產生 Gerber／鑽孔／座標／BOM／PDF。

## [1.3] — 原設計重繪（基準線）
- 依原設計 V1.3（Altium，2025-08-28 PDF 原理圖）在 KiCad 10 重繪原理圖，電路**完全不變**，作為改版比對基準。
- 位號對照：原 `Relay1–Relay4` → `K1–K4`（KiCad 慣例）。
- 自訂封裝（`hardware/lib/WOOW.pretty`）焊盤座標量測自原 Gerber。
- 新增 V1.3 PCB：`scripts/import_v13_pcb.py` 把原 Gerber 銅箔（走線、過孔）轉成 KiCad，零件以焊盤對齊放回原位，鋪銅照原設計 0.127mm。
  - 原理圖 vs 銅箔：0 個網路衝突、schematic parity 0 → 重繪原理圖已被實際銅箔驗證。
  - 修正：S1 指撥開關 1/2 對應 Mode_bit1/Mode_bit0（依銅箔）。
- 新增安規 DRC 規則 `WO30109_FreshAir.kicad_dru`：市電↔低壓 6.4mm、市電網路間 2.5mm、銅到板邊 0.5mm。
  - V1.3 跑出 253 個市電↔低壓違規、15 個市電網路間違規（原設計全板只用 0.127mm 間距）。
