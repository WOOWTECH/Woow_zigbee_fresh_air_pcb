# 變更紀錄

格式參考 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，版號用 `硬體大版.小版`。

## [1.3] — 原設計重繪（基準線）
- 依原設計 V1.3（Altium，2025-08-28 PDF 原理圖）在 KiCad 10 重繪原理圖，電路**完全不變**，作為改版比對基準。
- 位號對照：原 `Relay1–Relay4` → `K1–K4`（KiCad 慣例）。
- 自訂封裝（`hardware/lib/WOOW.pretty`）焊盤座標量測自原 Gerber。
- 新增 V1.3 PCB：`scripts/import_v13_pcb.py` 把原 Gerber 銅箔（走線、過孔）轉成 KiCad，零件以焊盤對齊放回原位，鋪銅照原設計 0.127mm。
  - 原理圖 vs 銅箔：0 個網路衝突、schematic parity 0 → 重繪原理圖已被實際銅箔驗證。
  - 修正：S1 指撥開關 1/2 對應 Mode_bit1/Mode_bit0（依銅箔）。
- 新增安規 DRC 規則 `WO30109_FreshAir.kicad_dru`：市電↔低壓 6.4mm、市電網路間 2.5mm、銅到板邊 0.5mm。
  - V1.3 跑出 253 個市電↔低壓違規、15 個市電網路間違規（原設計全板只用 0.127mm 間距）。
