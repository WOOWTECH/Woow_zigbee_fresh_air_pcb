# V4.0 規格：Matter over Thread

狀態：**草稿**（2026-10-05）。標「待查證」的項目等技術查證回來再定。

## 1. 目標

把網路層從 Zigbee 換成 **Matter over Thread**，同一台裝置可以直接加入 Apple Home、Google Home、Home Assistant。

- **硬體不變**：V3.4 板子（ESP32-C6 內建 IEEE 802.15.4，Thread 和 Zigbee 共用同一顆射頻）。
- **控制邏輯不變**：`fa_core`（繼電器互鎖、PWM 保持、DI／DO 模式、433 遙控、按鍵）照用。
- **取代 Zigbee**：Zigbee 韌體不再維護（留在 git 歷史與 `v3.4` 標籤）。

## 2. 已定案的決定

| # | 決定 | 來源 |
|---|---|---|
| D1 | Matter over Thread 取代 Zigbee | 使用者，10/05 |
| D2 | 支援 HA、Apple Home、Google Home | 使用者 |
| D3 | 4 個繼電器 = 4 個開關（On/Off Plug-in Unit），**不做風扇** | 使用者 |
| D4 | 4 路 DI = 4 個接點感測器（Contact Sensor／Boolean State），DI 設成「全關」時也照樣回報 | 使用者 |
| D5 | HA 使用者可在 HA 用下拉選單改 DI／DO 模式與點動時間（Mode Select），**V4.0 就做** | 使用者 |
| D6 | Apple／Google 使用者用 NFC App 改進階設定（不用 Wi-Fi 熱點） | 使用者 |
| D7 | NFC 密碼改用**每台 128-bit 隨機金鑰**，標籤在 Matter QR 旁多印一個 App QR；NFC 協定 v2（原本想用 Matter 配對碼，查證後不可行，見 §5） | 使用者，10/06 改選 |
| D8 | 測試先用使用者的 ESP32-C6 開發板（LED 代替繼電器），之後再上 V3.4 打樣板 | 使用者 |

## 3. Matter 資料模型（裝置組成）

| Endpoint | 裝置類型 | 叢集 | 對應 |
|---|---|---|---|
| 0 | Root Node | Basic Information、General Commissioning、Network Commissioning（Thread）… | esp-matter 預設 |
| 1–4 | On/Off Plug-in Unit (0x010A) | On/Off、Identify、Groups、Scenes | K1–K4 |
| 5–8 | Contact Sensor (0x0015) | Boolean State、Identify | DI1–DI4（接線 DI 或遙控器任一接通＝`StateValue` true） |
| 9–… | Mode Select (0x0027) | Mode Select | 每路的 DI 模式、DO 模式、點動時間（見 §4） |

- 每個 endpoint 用 Fixed Label／User Label 標名稱（例如「K1」「DI1」），讓 HA 的實體名稱分得清楚。
- 互鎖：App 同時打開同組兩路時，後到的贏，前一路自動關，並回報給所有 controller（和 Zigbee 版行為相同）。
- 同時吸合上限（電源預算）超過時：拒絕該指令並把 On/Off 屬性打回實際狀態。

## 4. 進階設定（Mode Select，給 HA）

每路 3 個下拉選單，4 路共 12 個：

| 選單 | 選項 | 對應 `fa_io_cfg_t` |
|---|---|---|
| DI k 模式 | 全關／按一次／持續 | `di_mode[k]` |
| K k 模式 | 自鎖／點動／互鎖 | `do_mode[k]` |
| K k 點動時間 | 0.5／1／2／5／10／30 秒、1／5／10／30／60 分 | `jog_ms[k]` |

- **待查證**：HA 是否把每個 Mode Select endpoint 都建成 `select` 實體；Apple／Google 遇到 Mode Select endpoint 是隱藏還是顯示「不支援」。如果 Apple 會顯示一堆「不支援」的配件，改成廠商自訂叢集（Apple／Google 一定忽略），HA 則需要另外處理。
- 設定改了：存 NVS、更新 NFC 的 STATE（gen＋1），NFC 與 HA 兩邊永遠是同一份設定。
- 點動時間：任意毫秒值只能用 NFC 設；HA 選單只提供上表的級距，NFC 設了不在級距內的值時，選單顯示最接近的級距。

## 5. 配對與 NFC

- **配對方式**：藍牙（BLE），用 Matter QR code 或 11 位數手動配對碼。配對完成後關閉藍牙。
- **NFC 協定 v2（已完成）**：每台 128-bit 隨機金鑰，標籤印 App QR `WONFC:2:<UID>:<secret>`。不用 Matter 配對碼的原因：裝置執行時拿不到配對碼（工廠資料只存 SPAKE2+ 驗證資料），而且 27 bit 可被離線暴力破解，破了連配對秘密一起洩漏。規格見 `docs/nfc-protocol.md` §5。
- **NFC 一碰配對**（**待查證**）：Matter 規格允許把配對資料放在 NFC 標籤，ST25DV 可以放 NDEF。要確認 iOS／Android 是否支援。

## 6. 按鍵與指示燈

| 操作 | 動作 |
|---|---|
| 短按 | 互鎖群組循環（同 V3.x） |
| 按住 3–8 秒放開 | 遙控器學習 20 秒 |
| 按住 ≥10 秒放開 | 恢復出廠：清 Matter fabric、遙控器、DI／DO 設定，重新開放配對 |

| 燈號 | 意義 |
|---|---|
| 慢閃 | 尚未配對（可配對） |
| 恆亮 | 已配對、Thread 已連線 |
| 兩短一長 | 已配對但 Thread 斷線 |
| 快閃 | 遙控器學習中／Identify |

## 7. 開發與測試計畫

- **建置**：ESP-IDF v5.5.5 ＋ esp-matter release/v1.6（commit `c6607128`）。安裝腳本見 CI（`.github/workflows/firmware-ci.yml`），需要 `python3-dev`。
- **沒有 Thread 邊界路由器時的測試**：同一份程式另編一個「Matter over Wi-Fi」的測試版本（ESP32-C6 有 Wi-Fi），用這台電腦的 Linux chip-tool 透過藍牙配對，驗證資料模型、開關、感測器、Mode Select、互鎖。正式版本是 Thread。
- **要實機驗證的項目**：Apple Home／Google Home／HA 的實際顯示（需要 Thread 邊界路由器）。
- **主機單元測試**：`fa_core` 照舊；新增「Matter 屬性 ↔ fa_io 設定」轉換的單元測試（點動級距、選項對應）。

## 8. 程式架構

```text
fa_core（純 C，不變）── app_main.c（任務、GPIO、NVS）── fa_net.h（網路層介面）── fa_matter.cpp（esp-matter）
                                          └── fa_nfc_port.c（NFC，協定 v2）
```

`fa_net.h` 沿用 `fa_zigbee.h` 的 4 個函式（start、joined、sync、factory_reset），再加：
- 回報 DI 狀態（接點感測器）；
- 設定改變時更新 Mode Select 屬性；
- Mode Select 被 controller 改時呼叫 app 的套用函式（和 NFC 共用 `nfc_apply_cfg` 那條路）。

## 9. 不在 V4.0 範圍

- Matter 認證（CSA 會員、DAC 正式憑證）：開發期用測試憑證，配對時 Apple／Google 會跳「未認證」提示。
- OTA 韌體更新（Matter OTA Provider）：之後版本。
- Wi-Fi 版本出貨：只當測試用。
