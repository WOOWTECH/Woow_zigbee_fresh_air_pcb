# 變更紀錄

格式參考 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，版號用 `硬體大版.小版`。

## [3.0] — ESP32-C6 單晶片＋SYN480R

把 Tuya ZS3L（JLC 無料）與 Ebelong EPA09-4D（無規格書、JLC 無料）換成 JLC 有庫存、可直接貼片的料，順便把 STM32 拿掉。

### 電路
- U1：STM32F103C8T6 ＋ U3 Tuya ZS3L → **ESP32-C6-WROOM-1-N8**（C5366877）。ESP32-C6 內建 IEEE 802.15.4，直接跑 Zigbee 3.0（ESP-Zigbee SDK），也有 Wi-Fi 6／BLE 5。ESP32-H2 模組在 JLC 全部 0 庫存，所以選 C6。
- RF1：EPA09-4D（4 路已解碼模組）→ **U3 SYN480R**（C916347，JSMSEMI）＋Y1 13.52127MHz/20pF（C654957）＋LC 匹配（C15 6.8pF、C16 1.8pF、L3 27nH、L4 47nH），照規格書 433.92MHz 典型應用。SHUT 接地（常開）、SQ 經 R14 0R 接地（關靜噪，靈敏度多 3dB）。**遙控器解碼（EV1527/PT2262）改由 ESP32 韌體做**（RMT 或 GPIO 中斷量脈寬），可學習多支遙控器。
- ANT1：433MHz 天線焊點（1/4 波長 17.3cm 導線或彈簧天線，手焊；JLC 無 433 天線庫存）。
- 腳位：繼電器 IO6/IO7/IO0/IO1（避開 strapping 腳，閘極 10k 下拉保證開機時不吸合）、指撥 IO10/IO11、燈 IO2、433 資料 IO23、按鍵 IO9。
- B1 改接 IO9（BOOT）：上電時按住＝進下載模式，平時當配對／重置鍵；R2 10k 上拉。IO8 加 R3 10k 上拉（下載模式需要 IO8=1）。EN 用 R1 10k／C5 1µF 延遲（Espressif 建議值）。
- P2：SWD 1×4 → 燒錄座 2×3（3V3、GND、TXD0、RXD0、EN、BOOT），接 USB-UART 轉接板用 esptool 自動燒錄。1×6 直排會壓到 H3 固定孔，所以改 2×3。
- 移除：C3、C4、C10、R13、TP1，與 ZS3L↔STM32 交握線 Input_1–4／Output_1–4。
- 新增自訂符號 `ESP32-C6-WROOM-1`、`SYN480R`、`Crystal_4P`（`scripts/make_symbols.py`）與封裝 `Espressif_ESP32-C6-WROOM-1`（依規格書 v1.4 Fig.10-1，含天線下方兩層禁銅區）。

### PCB
- 市電區、繼電器、電源、端子、固定孔完全沿用 V2.0（`scripts/upgrade_v30_pcb.py` 從 git 標籤 `v2.0` 的 PCB 開始，**不需要原始 Gerber**）。
- ESP32-C6 模組放在原 ZS3L 位置（左緣），天線端貼板邊、下方禁銅；SYN480R 放在原 RF1 位置，ANT 腳朝右，433 天線焊點遠離 ESP32 天線。
- 一鍵重建：`scripts/build_v30.sh`。

### ⚠️ 電源預算（要韌體配合）
IRM-02-12 額定 12V 167mA。ESP32-C6 Zigbee 發射 +12dBm 峰值 185mA@3.3V，折合 12V 側約 58mA（降壓效率 88%）；4 顆繼電器全吸 133mA → 峰值約 192mA，**超過額定**。
- 修正計算：路由器常態是**收訊** 73mA（12V 側 23mA），4 顆全吸持續 159mA 在額定內；超額的是發射峰值。韌體預設同時吸合 ≤3 顆、發射 +10dBm（峰值 158mA）、不啟動 Wi-Fi。
- 硬體根治：下一版改 IRM-03-12（250mA，C6640065），但封裝腳位完全不同，市電要重新佈線。

## [4.0.0-dev] — V4 Matter 開發中（未發布）

### NFC 協定 v2（已完成）
- 金鑰由每台 8 位數 PIN 改成 **128-bit 隨機金鑰**：第一次開機產生，另開 bootloader 熵源確保真亂數；存 NVS，恢復出廠也保留。
- 標籤在 Matter QR 旁多印一個 **App QR**：`WONFC:2:<UID>:<secret>`，57 字元、QR 英數模式。開機 log 印出這行。
- 標頭版本改成 2；v1 的 REQUEST／STATE 一律回「格式錯」。v1 沒有出貨，所以不做相容。
- 原本想用 Matter 配對碼當金鑰，查證後不可行：裝置執行時拿不到配對碼，而且 27 bit 可被暴力破解。
- 驗證：C 與 Python 新向量逐 byte 一致，主機測試 40/40；`docs/nfc-protocol.md` 已同步。

### 網路層：Zigbee → Matter（編譯通過，尚未上機）
- `fa_net.h` 網路層介面、`fa_modes`（HA 下拉選單 ↔ 設定）、`docs/v4-matter-spec.md` 規格草稿。
- 新增 `main/fa_matter.cpp`（esp-matter release/v1.6，ESP-IDF v5.5.5），刪除 `fa_zigbee.c/.h`。Endpoint：
  1–4 插座（K1–K4）、5–8 接點感測器（DI1–DI4）、9–20 Mode Select（每路 DI 模式／K 模式／點動時間）。
- 互鎖、同時吸合上限沿用 `fa_core`；controller 開了被拒的那一路時，屬性會打回實際狀態。
- Thread 發射功率限制在 `CONFIG_FA_TX_POWER`（預設 10 dBm，原 `FA_ZB_TX_POWER`）：IDF 802.15.4 預設 +20 dBm 超過 IRM-02-12 電源預算。
- 恢復出廠只清 Matter 的 NVS；NFC 金鑰、DI／DO 設定在 "fa" namespace 不受影響（設定由按鍵／NFC 的重置流程另外清）。
- 分割表改成 8MB：雙 OTA 各 3MB、`fctry` 給 Matter 工廠資料。映像 1.68 MB（ota_0 的 53%）。
- 另有「Matter over Wi-Fi」測試版（`sdkconfig.defaults.wifi_test`）：沒有 Thread 邊界路由器時用 chip-tool 驗證。
- CI：建置改用 `espressif/idf:v5.5.5`＋固定版本的 esp-matter（快取），Thread 與 Wi-Fi 兩版都編。
- NFC STATE 回報的韌體版本改為 4.0.0、硬體版本 3.4。
- 狀態燈抽成 `fa_core/fa_led`（可主機測試），新增「已配對但斷線＝兩短一長」；Matter Identify 會讓燈快閃。
- EP1–8 加 Fixed Label `ha_entitylabel`（K1–K4、DI1–DI4），HA 用它命名 entity；Matter 工廠資料改讀 `fctry` 分區。
- **修正（開發板上機發現）**：esp-matter 預設 endpoint 上限 16，EP16–20（後 5 個下拉選單）建立失敗；上限改 21，並加 `static_assert` 讓設定太小時編譯失敗。
- 開發板模式 `CONFIG_FA_DEVKIT_RGB`：板載 WS2812（GPIO8）用顏色顯示 K1–K4、亮滅顯示狀態（RMT 自寫驅動，不加相依）；`sdkconfig.defaults.devkit` 給 Thread 開發板用。
- 開發期廠商／產品名稱：`main/chip_project_config.h`（WOOWTECH／WO30109 新風控制器／硬體 V3.4），原本顯示 TEST_VENDOR／TEST_PRODUCT。
- 開發板模式多一個 Matter 彩色燈（EP21，Extended Color Light）：controller 開燈時板載 RGB 顯示它設的顏色／亮度／色溫，關燈回到狀態顯示；`fa_core/fa_color`（xy、色溫 → RGB，主機測試）。
- 上機驗收清單 `docs/verification/V4.0-checklist.md`（開發板 Wi-Fi／Thread／V3.4 板三階段，63 項）；查證筆記 `docs/v4-matter-research.md`。

## [3.4] — 照外殼 4-02-3 放回原板框

外殼是精鋼塑模 **4-02-3**（35mm 導軌盒，外形 88×72×59mm，螺絲間距 50×50mm）。原板 63.4×83.08mm 本來就是照它做的。V3.2（加高到 93mm）、V3.3（加高到 107mm）**放不進外殼**，V3.3 的板上 NFC 線圈也離前蓋太遠、讀不到。完整依據：[V3.4 驗證報告](docs/verification/V3.4.md)。

### 電路
- **J5**：改 2.54mm 7P（KEFA KF2EDGR-2.54-7P，C577599），和 P1 共用上方開窗；焊盤離市電 9.08mm。
- **NFC**：拿掉板上線圈，改 **J6**（JST SH 2P，C160388）接 FPC 天線貼前蓋內側；C22／C24 依天線電感選值，預設不上件。
- **P2**：排針改背面 2×3 燒錄測試點。
- **B1**：改 TS-1088（C720477，基礎料），放在 J5 後方（開蓋才按得到；學遙控器、恢復出廠可改用 NFC App）。
- **GPIO**：和 V3.3 相同，韌體不用改。

### PCB
- 板框 63.4×83.08mm、固定孔不變。`scripts/build_v34.sh`：從 V3.1 換件擺件，低壓全部交給 Freerouting 重佈。
- ERC 0；DRC 錯誤 0、未連線 0、原理圖一致性 0。
- CPL：J6 照規則表會是 0°，以 JLC 封裝驗證後改成 180°。

### 工具
- `mains_router.board_height()` 改用板框線段端點計算（原本會把板邊線寬算進去，格點偏 0.05mm）。
- 新封裝：`TerminalBlock_Pluggable_1x07_P2.54mm_Horizontal`（含 3D 模型）、`ProgPads_2x03_P2.54mm`、`SW_SPST_TS-1088`。
## [3.3.1] — NFC 設定韌體

- **協定與驗證**（純 C、主機可測）：
  - `fa_nfc`：STATE／REQUEST／ACK 格式、gen 樂觀鎖、HMAC 簽章、mailbox 指令（GET_STATUS、IDENTIFY、LEARN_REMOTE、FACTORY_RESET，以 challenge 防重送）。
  - `fa_sha256`：SHA-256／HMAC-SHA256。
- **為什麼由 ESP32 驗簽**：ST25DV 的 RF 密碼 I2C 端無法設定（規格書 Table 60），所以不用晶片的 RF 寫保護，改由 ESP32 驗簽。每台 8 位數 PIN 第一次開機產生、存 NVS。
- **ESP32 端**：
  - `st25dv.c`：I2C 驅動（16-byte 分列寫入＋ACK polling、I2C 密碼開 FTM、mailbox）。
  - `fa_nfc_port.c`：每 100ms 輪詢。
  - `app_main`：套用設定時，若互鎖分組改變就先全關再重新分組。
- **板子版本與腳位**：`board.h` 以 menuconfig 選 V3.3／V3.2；V3.3 的 DI 接 IO3／IO15／IO22／IO21、NFC I2C 接 IO20／IO19。
- **給 App 開發**：[`docs/nfc-protocol.md`](docs/nfc-protocol.md) 規格，以及 [`tools/nfc_ref.py`](tools/nfc_ref.py) 參考實作（只用標準函式庫）。
- **驗證**：
  - 主機測試 36 個：SHA-256 用 FIPS 180-2 向量、HMAC 用 RFC 4231 case 2／6、CRC 用 CCITT 檢查值；C 與 Python 逐 byte 對照測試向量。
  - `idf.py build` 在 V3.3 與 V3.2 兩種設定下都是 0 warning。

## [3.3] — NFC 設定介面（ST25DV04KC＋板上印刷線圈）

手機 App 貼近板子即可讀寫進階設定（DI／DO 模式、點動時間、遙控器清單），Apple／Google 使用者不用 Wi-Fi 熱點、Matter 也不必斷線。完整依據：[V3.3 驗證報告](docs/verification/V3.3.md)。

### 電路
- **U6 ST25DV04KC**（C3304276）：ISO 15693 NFC＋I2C，4Kbit EEPROM、256B mailbox。GPO 不接（韌體輪詢），V_EH 不用。
- **L5 板上印刷線圈**：14.9×14.6mm、8 圈、0.2／0.2mm，1.277µH（`scripts/nfc_coil.py`，以實際封裝銅箔計算）。
  - C22 75pF C0G（C1681）＋晶片內建 28.5pF → 13.84MHz。
  - C24 微調空位（不上件）。
- **I2C**：SDA＝IO20（U1 第 18 腳）、SCL＝IO19（第 17 腳），各接 10k 上拉；C23 100nF 去耦。
- **DI_1–4 改接 IO3／IO15／IO22／IO21**：和 I2C 照左右順序排，佈線不交叉。**韌體 `board.h` 要跟著改**（NFC 韌體 PR）。

### PCB
- **J5 改到左側板邊**：DI 電線改從左側出，由下往上依序是 +12V、IN1–4、COM、GND。左上角留給 NFC 線圈；左半段上緣提高到 107.2mm（右半段維持 83.08mm）。
- **一支腳本重建兩版**：`scripts/upgrade_v32_pcb.py` 以 `WO_VER=3.2／3.3` 切換，兩版都從 V3.1 重建。
- **線圈封裝** `WOOW:NFC_Coil_14.9x14.6mm_8T`：net tie，禁布區上下兩層禁止鋪銅和過孔。背面跨線由腳本畫成真正的走線。
- **驗證**：ERC 0；DRC 錯誤 0、未連線 0、不一致 0。

### 工具
- **`mains_router.py`**：
  - 禁布區外擴半線寬；
  - 禁止鋪銅的禁布區也算障礙；
  - 讀真實（L 形）板框。
- **`add_stitching.py --prune`**：同時移除懸空的 GND 過孔。
- **`make_footprints.py`**：新增 `nfc_coil()` 產生器。
## [3.2.1] — DI／DO 模式韌體

- 新增 `fa_io`（純 C）：每路 DI（全關／按一次／持續）× DO（自鎖／點動／互鎖），固定 1 對 1；遙控器 4 鍵＝DI1–4 的分身（同一組設定、狀態取「或」，200ms 沒收到幀＝放開）。11 個主機單元測試（共 31 個）。
- `fa_relays` 互鎖群組改由 DO 模式決定（`fa_relays_init_group`），取代指撥；按鍵風速循環跟著這組。
- `board.h`：DI1–4 = IO22/IO21/IO20/IO19；拿掉指撥腳。設定存 NVS `fa/io_cfg`，出廠預設由 menuconfig 選（預設三段風速＋1 路、DI 持續、點動 1 秒），恢復出廠一併清除。
- `idf.py build` 0 warning（556738 bytes）。

## [3.2] — 4 路光耦 DI 輸入、拿掉指撥

板子上緣左半段加長 10mm 放 4 路 DI（L 形，P1 前方維持原板緣）；市電區、繼電器區佈線不動。完整依據：[V3.2 驗證報告](docs/verification/V3.2.md)。

### 電路
- **J5**：7P 3.5mm 插拔端子，腳位 +12V／IN1–IN4／COM／GND。
  - 可接乾接點，也可接 5–24VDC（NPN、PNP 輸出都可以）。
  - +12V 經 **R23 1.5k** 限流，短路最多 8mA，可長期短路。
- **U5 TLP290-4**（C39031）：交流輸入光耦 ×4，隔離 2.5kVrms。輸入電阻 R15–R18 3.3k 1206，集極接 10nF（C18–C21）。
- **DI1–DI4**：接 IO22／IO21／IO20／IO19（U1 上排），用 ESP32 內建上拉，不另加上拉電阻。
- **拿掉 S1 指撥開關**：IO10／IO11 空出，模式改成軟體設定（韌體另開 PR）。

### PCB
- `scripts/upgrade_v32_pcb.py`：從 V3.1 的 PCB 一鍵重建，不跑 Freerouting。
  - 用格點 A* 佈新網路，和市電保持 6.5mm。
  - 433 匹配區上下兩層都設禁佈。
  - DI 長線自動試所有佈線順序。
  - 在新區域補縫合過孔，並自動接回 GND 孤島。
- ERC 0；DRC 錯誤 0、未連線 0、原理圖一致性 0。
- 板子變成 L 形：只有 J5 那一段上緣加高 10mm（83.08 → 93.08mm），P1（AC IN）前方維持原板緣，讓插頭與 230V 電線不會壓在低壓 GND 鋪銅上（固定孔不動，外殼要配合）。

### 工具修正
- `mains_router.py`：
  - 新增 `stamp_r`：細腳焊盤要真的接到焊盤中心。
  - 新增 `keep_cl`：清除自己焊盤的出腳通道時，別條網路的銅照樣算障礙。
  - 讀取封裝內建的禁佈區（ESP32 天線下方）。
  - 新增 `set_board()`：格點範圍跟著板高變動。
  - 以上預設值都維持舊行為。
- `add_stitching.py`：新增 `STITCH_REGION`（只在指定範圍加過孔）與 `--islands`（接回 GND 孤島）。
- `assign_3d.py`：改成冪等，不會每次多插一行空行。
- `make_jlc_files.py`：`--refresh-footprints` 預設只抓快取裡沒有的料號，並刪掉 BOM 已經不用的料號。
- `test_make_jlc_files.py`：BOM 已經不用的料號（S1），JLC 焊盤直接寫在測試裡。

## [3.1] — 433 匹配重新選值、繼電器 PWM 降壓保持

只換值、不改佈線（PCB 只動 8 顆的 Value／LCSC）。完整依據與模擬：[V3.1 驗證報告](docs/verification/V3.1.md)。

### 電路
- **433 匹配**：C15 6.8p→2.7p、C16 1.8p→2.7p（C162221，±0.1pF）、L3 27n→47n（C29683）、L4 47n→33n（C35050）。規格書典型應用值用同系列 Micrel 原廠實測輸入阻抗算，峰值落在 210–230MHz，433.92MHz 失配 8–13dB；新值以「三個晶片阻抗 × 元件公差」最差情況選出，失配 3.1–5.3dB（`scripts/rf_match_opt.py`、`sim/rf_match.cir`）。
- **D1–D4**：1N4148W → B5819W Schottky（C8598，基礎料、同 SOD-123），Vf 約 0.3V，符合 Omron PWM 保持驗證條件（≤0.4V）。

### 韌體
- **繼電器 PWM 降壓保持**（`FA_RELAY_PWM_HOLD`，預設開）：全壓 100ms 後改 LEDC 20kHz、60% 保持，每顆 12V 端 33.3→14.4mA（`sim/relay_pwm_hold.cir`）。`FA_MAX_RELAYS_ON` 預設 3→4，4 顆全開最壞約 135mA＜IRM-02-12 額定 167mA。
- 新增 `fa_coil` 模組與 3 個主機單元測試（共 20 個）；關掉 PWM 卻設 4 顆、或吸合間隔 ≤ 吸合時間會編譯失敗。

### CI
- KiCad CI 的 SPICE 步驟加跑 `relay_pwm_hold.cir`，守門：保持電流 < 17mA、Vds 峰值 < 20V。

### ⚠️ 打樣要驗證
- 433：VNA 量 S11 或遙控器實測距離（晶片真實阻抗原廠沒公開）。
- PWM 保持：標準版 G5Q-1 沒有 Omron 的 PWM 背書（-PW 型才有，JLC 無料）；最高溫＋振動下實測保持與釋放。

## [3.0.3] — CPL 角度改用 JLC 實際封裝驗證

### 修正（會直接做出壞板）
- 在 JLC 下單頁 3D 檢視器逐顆核對 V3.0 的 CPL，背面 **Q1–Q4、U4、S1 都差 90°**（腳伸出方向和焊盤垂直）。原因：matthewlai 規則表依「封裝名稱」比對，但 JLC 擺件用的是**該 LCSC 料號自己的 EasyEDA 封裝**；C5224182 的 `SOT-23-3…BR` 要 180°（表上 −90°）、C780769 的 `TSOT-26` 要 270°（表上 180°）、C7421516 的 DIP 開關要 270°（表上沒有規則）。
- 正確角度：Q1–Q4 0°、U4 90°、S1 90°（原本 90°、0°、180°）。其他零件和 3D 檢視器一致。

### 改進
- `hardware/WO30109_FreshAir/jlc_footprints.json`：BOM 26 種料號在 EasyEDA 庫的焊盤座標快取（CI 不連 EasyEDA，它連續抓約 20 筆就回 403）。
- `make_jlc_files.py`：角度優先用快取的 JLC 封裝依焊盤編號比對 0/90/180/270 求解（正面 板上=R(θ)·E；背面 板上=MirrorX·R(θ)·E，已在 JLC 檢視器實測），唯一解才採用；對稱件、焊盤編號對不上、沒有快取才退回規則表，並列出「角度沒有經過驗證」的零件（目前 B1、RV1、U2，都是對稱或腳位不對稱的通孔件，插反裝不進去）。模組散熱焊盤被 EasyEDA 拆成多個編號時，容許剔除 10% 焊盤。
- 新增 `--refresh-footprints`（換料後更新快取，需 `pip install easyeda2kicad`）與 `--check`（有料號沒快取就失敗）。
- `scripts/test_make_jlc_files.py`：6 個單元測試（真實的 U3、Y1、D1、Q1、U4、S1 焊盤＋鏡像、對稱、散熱焊盤離群等情況）。KiCad CI 新增「JLC 下單檔」步驟，跑測試＋`make_jlc_files.py --check`，產物附在 artifact 的 `jlc/`。

## [3.0.2] — JLC 下單檔

- `scripts/make_jlc_files.py`：一次產出 JLC 的 Gerber＋鑽孔 zip、BOM、CPL（`output/jlc/`）。CPL 位置取焊盤中心、背面角度鏡射，旋轉修正用 matthewlai `cpl_rotations_db.csv`（與 kicad-jlcpcb-tools 相同比對規則）；沒有規則的自訂封裝（U1、K1–K4、B1）會列出來，下單時要在 JLC 預覽確認。
- JLC API 預審：2 層、63.4×83.08mm、無紅／黃警示。

## [3.0.1] — V3.0 韌體與驗證

- **韌體** `firmware/`（ESP-IDF v5.5.4＋esp-zigbee-lib v2）：Zigbee 3.0 路由器、4 個 On/Off 端點；指撥 4 種模式（4 路獨立／三段風速＋1／兩段風速＋2／4 路互斥）與先斷後通互鎖；同時吸合上限、線圈吸合錯開 50ms、發射 +10dBm；SYN480R 邊緣中斷＋EV1527 解碼、遙控器學習（NVS 存 8 支）；B1 短按風速循環、3 秒學習、10 秒恢復出廠；L1 狀態燈。
- 純邏輯 `firmware/components/fa_core/` 主機端單元測試 17 項（gcc＋ASan/UBSan）。
- CI：新增 `firmware-ci.yml`（單元測試＋`espressif/idf:v5.5.4` 編譯，產出 .bin）。
- 驗證報告 `docs/verification/V3.0.md`：電源預算、433 匹配模擬（`sim/rf_match.cir`）、EMC、BOM／庫存、Jobset。
- Jobset 的 BOM 原本欄位空白、產出 0 位元組，已修正。

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
