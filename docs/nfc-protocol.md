# WO30109 NFC 設定協定 v2（給 App 開發）

適用板子 V3.3 起（U6 ST25DV04KC）。App 貼近板子即可讀寫進階設定，不需要任何網路。

- **板子有沒有通電都能讀寫**：斷電時晶片靠手機 NFC 場供電，可以讀寫 EEPROM；設定會在下次開機套用。
- **mailbox 即時指令**：要板子通電才能用。

- **參考實作**：[`tools/nfc_ref.py`](../tools/nfc_ref.py)，只用 Python 標準函式庫，可以直接照著翻成 Swift／Kotlin。
- **韌體實作**：`firmware/components/fa_core/src/fa_nfc.c`。
- **測試向量**：兩份實作用同一組向量逐 byte 對照（§7）。App 也請用同一組向量做單元測試。

## 1. 晶片與 NFC 指令

| 項目 | 值 |
|---|---|
| 標準 | ISO/IEC 15693（NFC Forum Type 5） |
| iOS | Core NFC `NFCTagReaderSession(pollingOption: .iso15693)` → `NFCISO15693Tag` |
| Android | `NfcV`（`android.nfc.tech.NfcV`） |
| 區塊大小 | 4 bytes；使用者 EEPROM 共 128 塊（512 bytes） |
| 製造商代碼（IC Mfg code） | `0x02`（ST），自訂指令要帶 |

使用的 ISO 15693 指令（規格書 DS13519 Rev 4 §7.6.8、§7.6.12、§7.6.31–33；ST 自訂指令都要帶 IC Mfg code `0x02`）：

| 用途 | 指令 | 備註 |
|---|---|---|
| 讀多個區塊 | `0x23` Read Multiple Blocks | iOS：`readMultipleBlocks` |
| 寫單一區塊 | `0x21` Write Single Block | iOS：`writeSingleBlock`；每塊約 5ms |
| 讀 mailbox 長度 | `0xAB` Read Message Length（ST 自訂） | 回傳值＝長度−1 |
| 讀 mailbox | `0xAC` Read Message（ST 自訂） | 參數：MBPointer `00`、Number of bytes `00` → 回傳整則訊息 |
| 寫 mailbox | `0xAA` Write Message（ST 自訂） | 參數：MSGLength（長度−1）、資料；要等 HOST_PUT_MSG＝0（上一則回覆已讀走）才能寫 |
| 讀 mailbox 狀態 | `0xAD` Read Dynamic Configuration，位址 `0x0D`（MB_CTRL_Dyn） | bit1 HOST_PUT_MSG＝板子有回覆 |

iOS 用 `customCommand(requestFlags:customCommandCode:customRequestParameters:)` 送 0xAA–0xAD；Android 用 `NfcV.transceive()`，自行組 `flags, cmd, 0x02, params`。

## 2. EEPROM 配置（使用者記憶體）

| 位址（byte） | 區塊 | 內容 | 誰寫 |
|---|---|---|---|
| `0x000`–`0x03F` | 0–15 | 保留（之後放 NDEF：貼手機就開 App 下載頁） | — |
| `0x040`–`0x0DF` | 16–55 | **STATE**：目前設定 | ESP32 |
| `0x0E0`–`0x17F` | 56–95 | **REQUEST**（App 寫）／**ACK**（ESP32 回寫） | App ↔ ESP32 |
| `0x180`–`0x1FF` | 96–127 | 保留 | — |

所有多位元組欄位都是 **little-endian**。

### 2.1 共用標頭（12 bytes）

| 偏移 | 長度 | 欄位 | 值 |
|---|---|---|---|
| 0 | 2 | magic | `'W' 'O'`（`57 4F`） |
| 2 | 1 | type | 1＝STATE、2＝REQUEST、3＝ACK |
| 3 | 1 | version | 2 |
| 4 | 4 | gen | 設定代數（見 §3） |
| 8 | 2 | len | 標頭後面的內容長度 |
| 10 | 2 | flags | 0 |

### 2.2 設定內容（payload，84 bytes）

| 偏移 | 長度 | 欄位 | 說明 |
|---|---|---|---|
| 0 | 4 | di_mode[4] | 每路 DI：0 全關、1 按一次、2 持續 |
| 4 | 4 | do_mode[4] | 每路 DO：0 自鎖、1 點動、2 互鎖（設成互鎖的幾路自動成一組） |
| 8 | 16 | jog_ms[4] | u32，點動時間（ms），500–3,600,000 |
| 24 | 24 | name | UTF-8 裝置名稱，0 結尾，最多 23 bytes |
| 48 | 1 | n_remotes | 已學習的 433 遙控器數量（0–8） |
| 49 | 3 | — | 0 |
| 52 | 32 | remotes[8] | u32，EV1527 20 位元位址；只有前 n_remotes 個有效，其餘填 0 |

### 2.3 STATE（`0x040`，106 bytes）

`標頭(type=1, len=92)` ＋ `payload(84)` ＋ `fw_version u32`（0x00MMmmpp，例如 3.3.0＝`0x00030300`）＋ `hw_rev[4]`（ASCII，例如 `"3.3\0"`）＋ `crc16 u16`。

- **CRC**：對前 104 bytes 計算 **CRC-16/CCITT-FALSE**（多項式 0x1021、初值 0xFFFF、不反射、不 XOR；`"123456789"` → `0x29B1`）。
- **CRC 不符**：可能剛好讀到 ESP32 正在寫的半筆，請重讀。
- **ESP32 什麼時候重寫 STATE**：每次開機、每次設定改變後。

### 2.4 REQUEST（`0x0E0`，112 bytes）

`標頭(type=2, gen=所根據的 STATE.gen, len=84)` ＋ `payload(84)` ＋ `tag(16)`

- **tag**：`HMAC-SHA256(key, 前 96 bytes)` 的前 16 bytes。
- **key**：`SHA-256("WO30109-NFC-v2:" + secret)`。secret 是每台 16 bytes 的隨機金鑰，從標籤上的 App QR 取得（§5）。
- **寫入順序很重要**：先寫區塊 57–83（位址 `0x0E4` 起，也就是第 4 byte 以後的所有內容），**最後才寫區塊 56**（magic／type／version 所在的 `0x0E0`–`0x0E3`）。ESP32 只要看到 magic 和 type=2，就當作整筆已經寫完。
- **最後一塊不足 4 bytes**：補 0。112 bytes 剛好 28 塊，不用補。

### 2.5 ACK（`0x0E0`，16 bytes）

ESP32 處理完 REQUEST（每 100ms 檢查一次）後，會在同一個位置寫入：

`標頭(type=3, gen=REQUEST 所根據的 gen, len=4)` ＋ `status u8` ＋ `0 0 0`

| status | 意義 | App 該怎麼做 |
|---|---|---|
| 0 | OK，已套用 | 重讀 STATE（gen 會＋1） |
| 1 | 簽章錯 | 金鑰不對（App QR 掃錯台，或資料傳輸出錯） |
| 2 | gen 不符 | 設定在這之間被別處改過：重讀 STATE、讓使用者確認後再送 |
| 3 | 欄位不合法 | 模式超出範圍、點動時間超出範圍、名稱沒有 0 結尾，或遙控器清單裡有沒學過的位址 |
| 4 | 格式錯 | magic／type／version／len 不對 |

**板子沒通電時**：寫進去的 REQUEST 會留在 EEPROM，下次開機才處理。App 可以顯示「已寫入，下次通電時套用」。

## 3. gen（設定代數）與重送保護

- STATE.gen 每次設定改變就＋1。改變的來源包括：NFC、遙控器學習、恢復出廠，以及之後的 Matter／HA。
- REQUEST 必須帶「讀到的 STATE.gen」。ESP32 只接受 `REQUEST.gen == 目前 gen`。
- 所以：
  - **錄下來的舊請求重送無效**：gen 已經往前了。
  - **不會蓋掉別人剛改的設定**：樂觀鎖。
- 恢復出廠**不會**把 gen 歸零。

## 4. mailbox 即時指令（板子要有電）

### 4.1 流程

1. 讀 `MB_CTRL_Dyn`，確認 mailbox 已開啟（bit0 MB_EN＝1）。
2. 用 Write Message（`0xAA`）寫入請求。
3. 每 20–50ms 讀一次 `MB_CTRL_Dyn`，等 bit1 HOST_PUT_MSG＝1（ESP32 每 100ms 輪詢一次）。
4. 用 Read Message Length（`0xAB`）＋ Read Message（`0xAC`）讀回覆。

### 4.2 格式

| 方向 | 格式 |
|---|---|
| App → 板子 | `'M'(4D)` `cmd` `seq` `len` `payload[len]` ［`tag16`，只有需授權的指令］ |
| 板子 → App | `'R'(52)` `cmd` `seq` `status` `len` `payload[len]` |

- **需授權指令的 tag**：`HMAC-SHA256(key, 'M' cmd seq len payload ‖ challenge(u32 LE))` 的前 16 bytes。
- **challenge**：由 GET_STATUS 的回覆取得；**每次 GET_STATUS 都會換新**。授權指令不論成功或失敗，用過一次就作廢。所以要先 GET_STATUS，再送授權指令。

| cmd | 名稱 | 授權 | 回覆內容 |
|---|---|---|---|
| `0x01` | GET_STATUS | 不需要 | 20 bytes（見下表） |
| `0x02` | IDENTIFY | 不需要 | 無；LED 快閃 5 秒，用來找是哪一台 |
| `0x10` | LEARN_REMOTE | 需要 | 無；進入遙控器學習 20 秒（按遙控器任一鍵完成，STATE 隨後更新） |
| `0x7F` | FACTORY_RESET | 需要 | 無；回覆後 0.5 秒重置並重開機 |

**GET_STATUS 回覆內容（20 bytes）**

| 偏移 | 長度 | 欄位 |
|---|---|---|
| 0 | 4 | gen |
| 4 | 1 | relays：bit k＝K(k+1) 吸合 |
| 5 | 1 | di：bit k＝DI(k+1) 接通（接線或遙控器） |
| 6 | 1 | net：0 未入網、1 已入網 |
| 7 | 1 | flags：bit0 遙控器學習中 |
| 8 | 4 | fw_version |
| 12 | 4 | uptime_s |
| 16 | 4 | challenge |

status 碼和 §2.5 相同，另外 5＝未知指令。

## 5. 金鑰、App QR 與安全性

- **金鑰怎麼來**：每台 **128-bit 隨機金鑰（secret）**，第一次開機用硬體亂數產生（同時開 bootloader 熵源，確保第一次開機、射頻還沒啟動時也是真亂數），存在 NVS，**恢復出廠也保留**，標籤上的 QR 才會一直有效。
- **App QR**：開機 log 會印出 `NFC 就緒（協定 v2），gen …；App QR：WONFC:2:…`。產線燒錄後讀出這行，印成標籤上 Matter QR 旁邊的第二個 QR。

  | 欄位 | 內容 |
  |---|---|
  | 格式 | `WONFC:2:<UID>:<secret>` |
  | UID | 16 位大寫十六進位，**MSB 在前**（和手機讀到的 ISO 15693 UID 顯示順序相同，`E002` 開頭） |
  | secret | 32 位大寫十六進位 |
  | 長度 | 57 字元；全部是 QR「英數模式」字元，QR 可以做得很小 |

  App 掃一次就把「UID → secret」記起來；之後碰到 NFC，用讀到的 UID 找對應的 secret。UID 對不上表示掃錯台。
- **為什麼不用 Matter 配對碼**：
  - 裝置執行時拿不到配對碼：Matter 工廠資料只存 SPAKE2+ 驗證資料，`GetSetupPasscode()` 回傳「未實作」。
  - 配對碼只有 27 bit，當 NFC 金鑰可以被離線暴力破解，破了連 Matter 配對的秘密也一起洩漏。
- **為什麼不用晶片的 RF 密碼**：ST25DV 的 RF 密碼 I2C 端無法設定（規格書 Table 60 寫明 I2C「No access」），只能由手機端寫入，產線就得逐台用手機設密碼。所以改由 ESP32 驗簽：
  - **讀取不設限**：STATE 裡沒有機密資料。
  - **寫入要簽章**：誰都能寫 EEPROM，但只有帶正確簽章、而且根據最新 gen 的 REQUEST 才會被套用；其他寫入會收到 ACK 錯誤碼，下次開機 STATE 也會被重寫回正確內容。
  - **遙控器只能刪、不能加**：加入新遙控器一定要在現場學習（按鍵或 LEARN_REMOTE 指令），避免用 NFC 偷偷塞一支別人的遙控器。
- **限制**：
  - 拿到 App QR（看得到標籤）的人可以改設定。標籤貼在外殼內側或配電箱內，和 Matter QR 一樣當成實體存取的憑證。
  - 開機 log 每次都會印出 App QR，接 UART 的人讀得到；UART 測試點在背面，要拆殼才碰得到，等同實體存取。
  - 128-bit 金鑰無法暴力破解（v1 的 8 位數 PIN 可以，這是改 v2 的主因之一）。

## 6. App 建議流程

0. **第一次**：掃標籤上的 App QR，記住「UID → secret」。
1. **連線**：掃描到 ISO 15693 標籤，UID 以 `E0 02` 開頭（ST）；用 UID 找到這台的 secret，找不到就請使用者先掃 App QR。
2. **讀狀態**：讀區塊 16–42（`0x040` 起 108 bytes）→ 解析 STATE；CRC 錯就重讀。
3. **板子有電時**：顯示即時狀態，送 GET_STATUS 取得繼電器／DI 狀態和 challenge。
4. **送出設定**：使用者修改後組 REQUEST（gen＝剛讀到的 STATE.gen）→ 先寫區塊 57–83，最後寫區塊 56。
5. **確認結果**：每 100ms 讀一次區塊 56–59（16 bytes），等 type＝3 的 ACK：
   - status＝0：重讀 STATE 並顯示「已套用」；
   - 板子沒電、一直等不到 ACK：顯示「已寫入，下次通電時套用」。

## 7. 測試向量（App 單元測試請比對）

輸入：

- secret＝`101112131415161718191A1B1C1D1E1F`（bytes 0x10–0x1F）
- UID＝`E002080000000001`
- di＝[2, 1, 0, 2]
- do＝[2, 2, 1, 0]
- jog_ms＝[1000, 1000, 5000, 1000]
- name＝「客廳新風」
- remotes＝[0x3A5F2, 0x1B007]

```text
key        d5c163b68da1f4fb1d51d02498707965f98ade59f5da675bccea78d184fd5fdc
App QR     WONFC:2:E002080000000001:101112131415161718191A1B1C1D1E1F
REQUEST    （gen 7）
           574f020207000000540000000201000202020100e8030000e803000088130000e8030000e5aea2e5bbb3e696b0e9a2a8
           00000000000000000000000002000000f2a5030007b00100000000000000000000000000000000000000000000000000
           91ac26d044df1d9d0fbeaa1e0b5fac63
STATE CRC  （gen 8、fw 0x00030301、hw "3.3"）前 104 bytes 的 CRC-16 = 0x6194
LEARN_REMOTE（seq 5、challenge 0xDEADBEEF）
           4d1005007b569d16854ce103a231b68817a8a725
```

產生方式：`python3 tools/nfc_ref.py`。韌體對應的測試是 `firmware/test/test_core.c` 的 `nfc_cross_language_vectors`。

## 8. 版本相容

- `version` 欄位目前是 **2**。v1（8 位數 PIN）只存在 V3.3 開發期韌體，沒有出貨，所以 v2 韌體**不相容 v1**：v1 的 REQUEST／STATE 一律回 status 4（格式錯）。
- 之後擴充 payload 時改成 3，並保留讀 v2 的能力。
- App 遇到 version 比自己認得的新，請只讀、不寫。
