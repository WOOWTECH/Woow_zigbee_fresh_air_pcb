# V4 Matter 研究筆記（ESP32-C6 / esp-matter release/v1.6）

調查日：2026-10-06。只讀，沒有改任何 repo。
來源版本：
- HA core `dev` @ `32b2389323785be71e67648eece7eaef424d3b98`（以下簡寫為 `HA@32b2389`）
  連結前綴：`https://github.com/home-assistant/core/blob/32b2389323785be71e67648eece7eaef424d3b98/homeassistant/components/matter/`
- 本機 esp-matter `release/v1.6` @ `c660712`；connectedhomeip @ `93abd8e6`（`~/esp/esp-matter/connectedhomeip/connectedhomeip`）
- esp-matter-mfg-tool 1.0.26（安裝在 `~/.espressif/python_env/idf5.5_py3.12_env/`）

標示「查不到／需實機」的項目代表沒有找到一手來源，不要拿來當結論。

---

## 0. 先看這裡：會影響 V4 的重大發現

1. **python-matter-server 已封存（archived）**。HA 現在依賴 `matter-python-client==1.4.0`，它是 matter.js 寫的新 OHF Matter Server 的 Python client
   （HA `manifest.json` 的 requirements；repo `home-assistant-libs/python-matter-server` 的 README 寫明「8.1.2 is the final version… moved to matterjs-server」；PyPI 上 matter-python-client 的 Source = `github.com/matter-js/matterjs-server/tree/main/python_client`）。
   命名規則寫在 HA integration 裡，server 只負責轉送 attribute，所以下面的命名結論不受影響。
2. **HA 只在「特定 VID/PID 白名單」上才讀 Fixed/User Label 來命名 entity**。韌體目前用測試 VID/PID `0xFFF1/0x8000`，剛好在白名單內，認的 label key 是 **`ha_entitylabel`**（見 §1）。正式量產換成自家 VID 後就失效，要另外向 HA 送 PR 把自家 VID/PID 加進清單。
3. 韌體 `partitions.csv` 的工廠分區叫 `fctry`（0x620000，大小 0x6000），但 `sdkconfig` 目前是 `CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="nvs"`（預設值），兩者**對不上**。要用工廠分區，必須把它改成 `"fctry"`（見 §5）。
4. connectedhomeip 的 test CD 目錄裡**沒有 `FFF1-8000` 的 CD**（只有 FFF2/FFF3 的）。要做 VID 0xFFF1/PID 0x8000 的工廠分區，得用 `chip-cert gen-cd` 自己簽一張 test CD（見 §5）。

---

## 1. HA entity／device 命名

### 裝置（HA device）名稱
`adapter.py` L186–191（HA@32b2389）：
```
name = (get_clean_name(basic_info.nodeLabel)
        or get_clean_name(basic_info.productLabel)
        or get_clean_name(basic_info.productName)
        or device_type.__name__)
```
同一個 node 的所有非 bridged endpoint 共用**同一個** HA device（`helpers.py` `get_device_id()`：非 bridged 的 postfix 固定是 `"MatterNodeDevice"`）。
**HA device 名稱 = Basic Information NodeLabel → ProductLabel → ProductName。**

### Entity 名稱
`entity.py`（HA@32b2389）：
- L134–142：如果「別的 endpoint 上也有同一個 primary attribute」（也就是多 endpoint 的同類 entity），而且 node 不是 bridge，`_name_postfix = str(endpoint_id)`。
- L143–149：平台的 translation key（例如 switch 的 `"switch"`）只有在**有 postfix 時**才會留下名稱；沒有 postfix 時 `_attr_name=None`，名稱就只剩裝置名。
- L151–154：如果 `_get_name_modifier()` 找到 label，就**用 label 值取代 postfix**。
- L159–183 `_find_matching_labels()`：同時讀 **UserLabel 與 FixedLabel 的 LabelList**，條件是 `lbl.label.lower() == <白名單 key>`，取 `lbl.value`。
- L42–62 `VENDOR_LABELING_LIST`：只有白名單裡的 VID/PID 才查 label：
  - `65521 (0xFFF1)`: PID `32768/32769/32770`（0x8000–0x8002）→ `["ha_entitylabel"]`
  - `65522 (0xFFF2)`: 同上
  - 另外有 Inovelli、TP-Link、Eve 等廠商各自的 key（`label`、`name`、`position`、`orientation` 等）
  - 這段是 HA PR #161974「Allow test vendor IDs to set Matter label」（commit `bce65d4f35`，2026-03-25）加進來的；label 命名功能本身來自 #154173（2025-10-22）。
- L285–293 `name`：如果設了 `_attr_name` 就直接回傳它；否則回傳 `f"{name} ({postfix})"`。

### 套用到本裝置（VID 0xFFF1 / PID 0x8000，`sdkconfig` L2874–2875）
| EP | 平台／來源 | 不加 label 的名稱 | 加 FixedLabel `ha_entitylabel=K1` 後 |
|---|---|---|---|
| 1–4 OnOffPlugInUnit | `switch.py` L291–301 `MatterPlug`，translation_key `switch`（L79） | `<裝置名> Switch (1)`…`(4)` | `<裝置名> Switch (K1)`…`(K4)` |
| 5–8 ContactSensor | `binary_sensor.py` L119–130，device_class DOOR | `<裝置名> Door (5)`…`(8)`（推論：沒有平台 translation key，用 device class 的名稱；沒有找到這個 case 的 snapshot） | `<裝置名> Door (DI1)`…`(DI4)` |

上表的名稱格式是依程式邏輯推出來的，並對照了 `tests/components/matter/snapshots/test_switch.ambr` 的實例（`'Eve Energy 20ECN4101 Switch (top)'`、`'Mock Cooktop Power (1)'`）。**實際字串要在 HA 實機上確認。**

**韌體要做的事：**
- 在 EP1–8 各建一個 Fixed Label cluster（0x0040），放一筆 `{label:"ha_entitylabel", value:"K1"}`（DI1 等依此類推）。key 大小寫不拘，因為比對時會 `lower()`。
- Fixed Label 的值來自 `DeviceInfoProvider::IterateFixedLabel()`（connectedhomeip `src/app/clusters/fixed-label-server/FixedLabelCluster.cpp` L30–76、`CodegenIntegration.cpp` L44–47）。韌體目前是 `CONFIG_NONE_DEVICE_INFO_PROVIDER=y`（`sdkconfig` L3042），這樣 LabelList 會是空的。要改用其中一種：
  - (a) `CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER=y` + `CONFIG_FACTORY_DEVICE_INFO_PROVIDER=y`，再用 mfg-tool 的 `--fixed-labels "1/ha_entitylabel/K1" "2/ha_entitylabel/K2" … "5/ha_entitylabel/DI1" …` 寫進 fctry（mfg_tool.py L720–732 會寫成 NVS key `fl-sz/<ep>`、`fl-k/<ep>/<i>`、`fl-v/<ep>/<i>`；`utils.py` L336–344 限制 key 與 value 都要 **少於 16 字元**，`ha_entitylabel` 是 14 字元，可以）；或
  - (b) `CONFIG_CUSTOM_DEVICE_INFO_PROVIDER=y` + `esp_matter::set_custom_device_info_provider()`，把 label 寫死在韌體裡（esp-matter `components/esp_matter/esp_matter_providers.cpp` L160–173；Kconfig L125–145）。
- 也可以用 User Label，HA 一樣會讀，但使用者或其他生態系可以改寫它，所以不建議拿來放「出廠名稱」。
- **限制**：換成正式 VID 後，這條路就不通了，除非 HA 把新 VID/PID 加進 `VENDOR_LABELING_LIST`。沒有通用的「讀 FixedLabel `name`／`label`」機制。
- 另一條路（只列出、沒有驗證）：用 Aggregator 加 Bridged Node 的架構，每個 endpoint 帶 BridgedDeviceBasicInformation.NodeLabel。這樣 HA 會把每個 endpoint 拆成獨立的 HA device，並用 NodeLabel 命名（`adapter.py` L195–230、`helpers.py` L77–79）。但這會改變 Apple 與 Google 那邊的呈現方式，而且原生裝置偽裝成 bridge 在認證上的可行性「查不到／需實機」。
- Mode Select **不會**影響 Plug 和 Contact 的命名。

## 2. HA 的 Mode Select 與 Contact Sensor

- **select entity**：`select.py` L313–326 的 discovery schema `key="MatterModeSelect"`，條件是 `required_attributes=(ModeSelect.CurrentMode, ModeSelect.SupportedModes)`，`secondary_value_is_not=[]`（**SupportedModes 是空清單時不會建 entity**），而且沒有 device_type 限制，所以每個有 Mode Select 的 endpoint 都會建一個 select。`entity_category=EntityCategory.CONFIG`：它會出現在裝置頁的「設定」區，不會進自動產生的儀表板。
- **名稱**：`select.py` L192–194，`if desc := cluster.description: self._attr_name = desc`。這樣名稱**就是 Description**（例如「DI1 模式」），不會加 `(9)` 這種 postfix，因為 `name` 有 `_attr_name` 時直接回傳，見 entity.py L287–289。完整顯示為 `<裝置名> DI1 模式`。沒有 Description 時用 translation `"Mode"`（`strings.json`），並加上 `(ep)`。
- **選項**：`options = [mode.label for mode in supportedModes]`，`current = label of CurrentMode`（L187–190）。**每個 label 在同一個 endpoint 裡要唯一**，因為反查是靠 label 字串比對。
- **寫入**：`async_select_option` 會送 **`ModeSelect.Commands.ChangeToMode(newMode=mode.mode)`**（L163–177），不是寫 attribute。
- **Contact Sensor**：`binary_sensor.py` L119–130，`device_class=DOOR`，`device_to_ha=lambda x: not x`（註解寫「value is inverted on matter」）。所以 **StateValue=true → is_on=False → HA 顯示「關閉」**；false 顯示「開啟」。這要求 endpoint 的 device type 是 ContactSensor（0x0015）。
  規格語意是 TRUE = closed／contact、FALSE = open／no contact。CSA Device Library 原文需要會員下載，「查不到」可公開引用的 CSA 原文；二手一致來源有 Espressif Arduino 文件 `MatterContactSensor`「true = closed, false = open」：https://docs.espressif.com/projects/arduino-esp32/en/latest/matter/ep_contact_sensor.html 。韌體在 DI 導通（接點閉合）時應該設 StateValue=true。

## 3. Apple Home／Google Home

**Google Home**（https://developers.home.google.com/matter/supported-devices ）
- 頁面列了 Contact Sensor（Boolean State，Matter 1.0，GHA 類型 Sensor）與 On/Off Plug-In Unit（On/Off、Level Control，Matter 1.0，GHA 類型 Outlet）。
- **Mode Select（0x0027）不在清單上**。原文：「Device types other than the ones listed here are not officially supported in the Google Home ecosystem.」
- Mode Select endpoint 會被隱藏、顯示成不支援，還是擋下配對：**查不到／需實機**。官方只提到一個 endpoint 遮蔽的例子（同時有 Light 與 Light Switch 時，endpoint 順序會影響哪個看得到）。
- Google Home APIs（給第三方 app 用）有 `ModeSelectTrait`（https://developers.home.google.com/reference/swift/GoogleHomeTypes/Enums/Matter ），但這和 GHA UI 會不會顯示是兩回事。
- 測試 VID 0xFFF1–0xFFF4：要先在 Google Home Developer Console 建一個 VID/PID 相同的 Matter integration，才能配對（https://developers.home.google.com/matter/integration/create ）。

**Apple Home**
- https://support.apple.com/en-us/102135 ：「The Home app currently supports these types of Matter accessories: air conditioners, bridges, lights, locks, outlets, switches, thermostats, blinds and shades, and sensors (motion, ambient light, contact, temperature, and humidity).」→ outlet 與 contact sensor 都支援。
- Mode Select：Apple Developer Forums thread 763865（2024-09）。開發者回報加了 Mode Select 之後「can not find the work mode in Apple Home… only the light related functions show in UI」，代表裝置能配對，但 Mode Select 在 UI 看不到。Apple DTS 的 Kevin Elliott 回覆：「HomeKit's matter support is fairly narrow and typically only replicates what's already available through the similar HomeKit accessory.」 https://developer.apple.com/forums/thread/763865
- 會不會擋配對：沒有官方明文，「需實機」。上面的論壇案例顯示有 Mode Select 也能配對。

## 4. NFC 配對（NDEF「MT:…」）

- **規格面**：Matter 1.4.1 加入了「Onboarding Info in NFC Tag」。CSA 原文：「allows manufacturers to embed the same information found in Matter QR codes into NFC tags… Users can simply tap their cell phone to the device for commissioning.」 https://csa-iot.org/newsroom/a-smarter-start-matter-1-4-1-makes-setup-easier/ 。CSA 公告沒有提到 Apple 或 Google 的支援狀態。
- **SDK 參考實作的格式**：connectedhomeip `src/platform/nrfconnect/NFCOnboardingPayloadManagerImpl.cpp` L45–60 用 `nfc_ndef_uri_msg_encode(NFC_URI_NONE, payload…)`，也就是 **NDEF URI record、URI identifier code 0x00，內容是完整的 `MT:...` 字串**，再做 Type 2 Tag 模擬。
- **Android**：connectedhomeip 的參考 app CHIPTool（不是 Google Play Services）會在 `AndroidManifest.xml` L37–41 註冊 `NDEF_DISCOVERED` + `scheme="mt"`；`CHIPToolActivity.kt` L142–160 要求剛好 1 個 message、1 個 record、URI scheme 是 `mt`，再把它轉成大寫交給 QR parser。**Google Play Services／Google Home app 會不會讀 MT: NFC tag：查不到／需實機**（Google Commissioning API 文件 https://developers.home.google.com/apis/android/commissioning 沒有提到 NFC）。
- **iOS**：Apple 支援頁只寫「follow the instructions to scan a code or hold your device near the accessory to add it」（https://support.apple.com/en-us/102135 ），沒有明說這適用於 Matter 的 MT: NDEF。iOS Home 會不會讀 Matter NFC payload：**查不到／需實機**。
- 注意：`SetupPayload.h` L106 的 `RendezvousInformationFlag::kNFC`（discovery bit 16）指的是「用 NFC 當配對傳輸」，不是「NFC tag 裡放 QR payload」。只放 onboarding payload 時，discovery bitmask 仍然設 BLE（2）。

## 5. 量產燒錄（esp-matter-mfg-tool）

文件：esp-matter `docs/en/developing.rst` L1077–1300（Factory Data Providers、Factory Partition、Flashing）與 L1365–1400（Supported Modes）；`docs/en/production.rst` L123–215。工具已搬到 https://github.com/espressif/esp-matter-tools/tree/main/mfg_tool （`tools/mfg_tool/README.md`）。

**sdkconfig（工廠分區路線，DAC 放在 fctry）**
```
CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER=y
CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER=y
CONFIG_FACTORY_PARTITION_DAC_PROVIDER=y            # esp-matter Kconfig L42（設了 FACTORY_DATA_PROVIDER 時預設就是它）
CONFIG_FACTORY_COMMISSIONABLE_DATA_PROVIDER=y      # Kconfig L73
CONFIG_FACTORY_DEVICE_INSTANCE_INFO_PROVIDER=y     # Kconfig L103
CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER=y         # 要 fixed labels／supported modes 才需要
CONFIG_FACTORY_DEVICE_INFO_PROVIDER=y              # 同上（Kconfig L134）
CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="fctry"   # 預設 "nvs"（chip Kconfig L1157–1161），必須改
# CONFIG_ENABLE_TEST_SETUP_PARAMS is not set       # developing.rst：用 Factory provider 時要關掉
```
`production.rst` L180–215 建議正式量產改用 secure-cert provider（`CONFIG_SEC_CERT_*`）。

**產生（VID 0xFFF1/PID 0x8000，用 test PAA 簽 DAC）**
1. test CD：repo 裡沒有 FFF1-8000，要自己產生（developing.rst L1220–1233）：
   `chip-cert gen-cd -f 1 -V 0xFFF1 -p 0x8000 -d 0x010A -c "CSA00000SWC00000-01" -l 0 -i 0 -n 1 -t 0 -K credentials/test/certification-declaration/Chip-Test-CD-Signing-Key.pem -C credentials/test/certification-declaration/Chip-Test-CD-Signing-Cert.pem -O TEST_CD_FFF1_8000.der`（`-d` 填主要 device type）
2. `esp-matter-mfg-tool -n 100 --paa -c credentials/test/attestation/Chip-Test-PAA-FFF1-Cert.pem -k credentials/test/attestation/Chip-Test-PAA-FFF1-Key.pem -cd TEST_CD_FFF1_8000.der -v 0xFFF1 -p 0x8000 --vendor-name … --product-name … --hw-ver 1 --hw-ver-str V4 --target esp32c6 --fixed-labels "1/ha_entitylabel/K1" … [--supported-modes …]`
   - `--paa` 會先產生 PAI 再簽 DAC；`--pai` 則直接拿輸入的 PAI 來簽（help 的「Input certificate type」）。
   - 不帶 `--passcode` 和 `--discriminator` 時，每台都隨機產生（help 原文）。`-n` 是台數，`--csv/--mcsv` 可逐台指定序號等欄位。
   - `-s` 的預設是 0x6000，和韌體的 fctry 大小相同。
   - `-dm` 的預設是 BLE。
3. 輸出在 `out/fff1_8000/<uuid>/`：`<uuid>-partition.bin`、`<uuid>-onb_codes.csv`（qrcode, manualcode, discriminator, passcode）、`<uuid>-qrcode.png`（mfg_tool.py L509–527、L632–656），另外有 summary CSV 與 `cn_dacs-*.csv`。
4. 燒錄：`esptool.py -p PORT write_flash 0x620000 <uuid>-partition.bin`（位址取自韌體的 `partitions.csv` fctry 欄；developing.rst L1293–1299 說要先查 partition label 再對位址）。

**Supported Modes 補充**：esp-matter 文件寫「ESP_MATTER uses factory partition to set the values of Supported Modes attribute」（developing.rst L1369），搭配 `StaticSupportedModesManager`。目前韌體用的是自己的 delegate，所以不必走這條路。

## 6. Linux 上不用 sudo 跑 chip-tool

- **GitHub releases**：project-chip/connectedhomeip v1.5.0.0–v1.6.1.0 都**沒有附 binary**（`gh api …/releases`：assets=0）。
- **snap**：Canonical IoT Labs 發佈的 `chip-tool`，stable 是 `v1.5.1.0+snap`（rev 315），beta 和 edge 是 master 的 `6170af84+snap`（2026-10-05）。官方指南只寫 `sudo snap install chip-tool`（connectedhomeip `docs/development_controllers/chip-tool/chip_tool_guide.md` L35–41）。
- **實測（2026-10-06，本機 Ubuntu glibc 2.39）**：`snap download chip-tool`（不需要 root）→ `unsquashfs -d chiptool_snap chip-tool_315.snap` → `TMPDIR=<dir> ./chiptool_snap/bin/chip-tool payload parse-setup-payload MT:Y.K9042C00KA0648G00`，**可以正常執行**，正確解出 VID 65521、PID 32768、discriminator 3840、passcode 20202021。
  - snap 的 base 是 `core24`，confinement 是 strict，plugs 有 network、network-bind、bluez、avahi-observe、process-control（`meta/snap.yaml`）。
  - command-chain 只做一件事：`export TMPDIR=$SNAP_USER_COMMON`。在 snap 外面執行時要自己設 `TMPDIR`，KVS 會寫到 `$TMPDIR/chip_tool_kvs`。
  - 動態函式庫用的是 host 的 libssl3 和 glib2；能跑是因為 host 是 24.04 並且和 core24 相容，其他發行版沒有驗證。
- **版本落差**：stable 是 v1.5.1，裝置是 1.6 分支。官方建議「always build the CHIP Tool and the Matter device from the same revision」（chip_tool_guide.md 的 Note）。如果要對齊，可以用 beta 或 edge，或自己編 `out/host`。本機 `connectedhomeip/out/host-chip-tool/` 今天 06:52 剛產生 build 目錄，但還沒有 chip-tool binary，似乎有人正在編譯。
- **BLE 權限**：Linux 版 chip-tool 透過 BlueZ D-Bus（`org.bluez`）操作 BLE。本機 `/usr/share/dbus-1/system.d/bluetooth.conf` 有 `<policy context="default"><allow send_destination="org.bluez"/></policy>`，**任何使用者都能對 bluetoothd 發 D-Bus**，不需要 sudo，也不需要 bluetooth 群組。本機也有 controller（`bluetoothctl list` 看得到 BC:09:1B:7F:F4:76）。用 snap 安裝時要 `sudo snap connect chip-tool:bluez`（https://github.com/canonical/chip-tool-snap README），但解包後在 snap 外執行沒有這層限制。**實際用 BLE 配對 Thread 裝置（需要 OTBR 的 dataset）在非 root 下能不能成功：需實機。** `bluetoothd --experimental --debug` 只是除錯建議（`docs/platforms/linux/debugging_tips.md` L38–58），不是必要條件。
