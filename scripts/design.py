"""WO_30109 Zigbee 新風控制器 — 電路的唯一真實來源（single source of truth）。

每顆零件：ref -> (symbol lib_id, value, footprint, LCSC, {pin: net})
  * net 為 None 表示不接（會在原理圖打 no-connect）。
  * VERSION="1.3"：照原設計 V1.3（2025-08-28 PDF 原理圖）重繪，不改任何電路。
  * VERSION="2.0"：改版，差異寫在 CHANGES。
  * VERSION="3.0"：STM32＋Tuya ZS3L＋EPA09-4D → ESP32-C6 單晶片（Zigbee 3.0）＋SYN480R 433MHz 接收，差異寫在 CHANGES。
  * VERSION="3.4"：照外殼 4-02-3（88×72×59 導軌盒）改回原板框 63.4×83.08：J5 2.54mm、NFC 外接 FPC 天線（J6）、P2 改測試點、B1 改小。
  * VERSION="3.3"：加 NFC（U6 ST25DV04KC＋板上線圈 L5），J5 改到左側板邊，左半段再加高。
  * VERSION="3.2"：加 4 路光耦 DI（J5、U5 TLP290-4）、拿掉指撥 S1，板子上緣加長 10mm。
  * VERSION="3.1"：只換值不改板：433 匹配 C15/C16/L3/L4 重新選值、D1–D4 改 Schottky B5819W（配合韌體 PWM 保持）。
"""
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"


def build(version="2.0"):
    P = {}

    def add(ref, lib, val, fp, lcsc, pins):
        P[ref] = dict(lib=lib, value=val, footprint=fp, lcsc=lcsc, pins=pins)

    v3 = version.startswith("3")
    v31 = v3 and tuple(map(int, version.split("."))) >= (3, 1)   # V3.1：433 匹配值、續流 Schottky
    v32 = v3 and tuple(map(int, version.split("."))) >= (3, 2)   # V3.2：4 路光耦 DI、拿掉指撥
    v33 = v3 and tuple(map(int, version.split("."))) >= (3, 3)   # V3.3：NFC（ST25DV04KC＋板上線圈）
    v34 = v3 and tuple(map(int, version.split("."))) >= (3, 4)   # V3.4：照外殼 4-02-3 改回原板框（DI 2.54mm、NFC 外接天線）
    v2 = version.startswith("2") or v3          # V3.0 沿用 V2.0 的市電、電源、繼電器

    # ---------------- 市電輸入 ----------------
    if v2:
        # V3.4：P1／F1／P3 改 JLC 代焊（使用者 10/07：「要 JLC 那邊都處理完」）。P1 KEFA KF2EDGR-5.0-2P（C441193，
        # 規格書建議孔 Ø1.6）；F1 Littelfuse 372 系列 TR5 保險絲 T3.15A（C178794）直接焊在 TR5 封裝（腳距 5.08、孔 Ø1.0），
        # 不用保險絲座：座子 JLC 能焊，但保險絲插進座子要人工
        add("P1", "Connector_Generic:Conn_01x02", "AC IN 230V", "WOOW:TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal",
            "C441193" if v34 else "", {"1": "AC_L_IN", "2": "AC_N"})
        add("F1", "Device:Fuse", "T3.15A 250V TR5" if v34 else "T3.15A 250V", "Fuse:Fuseholder_TR5_Littelfuse_No560_No460",
            "C178794" if v34 else "", {"1": "AC_L_IN", "2": "AC_L"})
        add("RV1", "Device:Varistor", "07D471K", "Varistor:RV_Disc_D7mm_W3.4mm_P5mm", "C28756",
            {"1": "AC_L", "2": "AC_N"})
        add("U2", "Converter_ACDC:IRM-02-12", "IRM-02-12", "Converter_ACDC:Converter_ACDC_MeanWell_IRM-02-xx_THT", "C7211213",
            {"1": "AC_L", "2": "AC_N", "3": "GND", "4": "+12V"})  # L/N 對調：佈線需要，IRM 輸入不分極性
    else:
        add("P1", "Connector_Generic:Conn_01x02", "AC IN", "WOOW:TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal", "",
            {"1": "AC_L", "2": "AC_N"})
        add("U2", "Converter_ACDC:IRM-02-12", "IRM-02-12", "Converter_ACDC:Converter_ACDC_MeanWell_IRM-02-xx_THT", "C7211213",
            {"2": "AC_L", "1": "AC_N", "3": "GND", "4": "+12V"})

    # ---------------- 12V -> 3.3V ----------------
    add("C6", "Device:C", "10uF 25V", C0603, "C96446", {"1": "+12V", "2": "GND"})
    if v2:
        # AP63203WU：固定 3.3V 輸出同步降壓，3.8–32V 輸入、陶瓷電容穩定（取代 LM1117 線性穩壓）
        add("U4", "Regulator_Switching:AP63203WU", "AP63203WU-7", "Package_TO_SOT_SMD:TSOT-23-6", "C780769",
            {"3": "+12V", "2": "+12V", "4": "GND", "5": "SW", "6": "BST", "1": "+3V3"})
        add("C12", "Device:C", "100nF", C0603, "C14663", {"1": "BST", "2": "SW"})
        add("L2", "Device:L", "3.9uH", "Inductor_SMD:L_Sunlord_SWPA4030S", "C96899", {"1": "SW", "2": "+3V3"})
        add("C7", "Device:C", "22uF 25V", C0805, "C45783", {"1": "+3V3", "2": "GND"})
        add("C13", "Device:C", "22uF 25V", C0805, "C45783", {"1": "+3V3", "2": "GND"})
        add("C14", "Device:C", "100nF", C0603, "C14663", {"1": "+12V", "2": "GND"})
    else:
        add("U4", "Regulator_Linear:AMS1117-3.3", "LM1117-3.3", "Package_TO_SOT_SMD:SOT-223-3_TabPin2", "C23984",
            {"3": "+12V", "2": "+3V3", "1": "GND"})
        add("C7", "Device:C", "10uF", C0603, "C96446", {"1": "+3V3", "2": "GND"})

    # ---------------- V3.0：ESP32-C6 + SYN480R ----------------
    if v3:
        # ESP32-C6-WROOM-1-N8：Zigbee 3.0（802.15.4）直接跑在模組上，取代 STM32 + ZS3L
        #   繼電器 IO6/IO7/IO0/IO1（非 strapping 腳；模組下排靠繼電器）；指撥 IO10/IO11（內部上拉）
        #   按鍵 = IO9（BOOT strapping：上電按住進下載模式，平時當配對/重置鍵）；IO8 上拉（下載模式需 =1）
        #   燈 IO2；433 資料 IO23；燒錄 UART0 + EN + IO9 拉到 P2
        esp = {str(n): None for n in range(1, 30)}
        esp.update({"1": "GND", "28": "GND", "29": "GND", "2": "+3V3", "3": "EN",
                    "6": "Relay_1", "7": "Relay_2", "8": "Relay_3", "9": "Relay_4",
                    "10": "IO8", "11": "Mode_bit0", "12": "Mode_bit1", "15": "BOOT",
                    "21": "RF_DATA", "24": "U0RXD", "25": "U0TXD", "27": "LED"})
        if v32:   # 指撥拿掉（IO10/IO11 空出）。DI1–4 接上排 IO22/IO21/IO20/IO19（20–17 腳）：U5 就在正上方約 16mm，
                  # 背面直上、不經模組底下；左右順序與 U5 輸出一致不交叉；避開 433 匹配網路（x>120.9）。皆非 strapping 腳
            esp.update({"11": None, "12": None, "20": "DI_1", "19": "DI_2", "18": "DI_3", "17": "DI_4"})
        if v33:   # 6 條線都從上方進 U1 上排，照左右順序一對一接、彼此不交叉（不這樣排，DI 與 I2C 長線在 U3 底下
                  # 互相交叉，佈線器無解）：DI_1–4 → 26（IO3）、23（IO15）、20（IO22）、19（IO21）；SDA → 18（IO20）、SCL → 17（IO19）。
                  # IO15 是 strapping 腳，但只在燒了 EFUSE_JTAG_SEL_ENABLE 時才讀（選 JTAG 來源），出廠不受 DI 電位影響
            esp.update({"26": "DI_1", "23": "DI_2", "20": "DI_3", "19": "DI_4", "18": "NFC_SDA", "17": "NFC_SCL"})
        add("U1", "WOOW:ESP32-C6-WROOM-1", "ESP32-C6-WROOM-1-N8", "WOOW:Espressif_ESP32-C6-WROOM-1", "C5366877", esp)
        add("C1", "Device:C", "22uF 25V", C0805, "C45783", {"1": "+3V3", "2": "GND"})       # 模組 3V3 腳旁（規格書 Fig.9-1）
        add("C2", "Device:C", "100nF", C0603, "C14663", {"1": "+3V3", "2": "GND"})
        add("R1", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "EN"})           # EN RC 延遲 10k/1uF（規格書建議）
        add("C5", "Device:C", "1uF", C0603, "C15849", {"1": "EN", "2": "GND"})
        add("R2", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "BOOT"})
        add("R3", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "IO8"})
        add("C8", "Device:C", "10uF", C0603, "C19702", {"1": "+3V3", "2": "GND"})
        add("C9", "Device:C", "100nF", C0603, "C14663", {"1": "+3V3", "2": "GND"})
        add("R4", "Device:R", "100R", R0603, "C22775", {"1": "LED", "2": "LED_A"})
        add("L1", "Device:LED", "Blue", "LED_SMD:LED_0603_1608Metric", "C965807", {"2": "LED_A", "1": "GND"})
        # SYN480R 433.92MHz ASK/OOK 接收。匹配拓撲同規格書典型應用：
        #   ANT1 ─┬─ C16 ─ GND ─┬─ L3 ─ GND，經 C15 串到 ANT 腳；ANT 腳 L4 到 GND
        # 值（V3.1）：規格書值（C16 1.8p、L3 27n、C15 6.8p、L4 47n）照同系列 Micrel 原廠實測輸入阻抗算，
        # 峰值落在 210–230MHz、433.92MHz 失配 8–13dB；改用 scripts/rf_match_opt.py 最差情況最佳化的值，
        # 433.92MHz 失配 3–5.3dB（sim/rf_match.cir）。打樣後用 VNA 量 S11 微調。
        #   Y1 13.52127MHz/20pF 接 RO-GND；SHUT 接地（常開）；SQ 經 R14 0R 接地（關靜噪，靈敏度 +3dB）
        add("U3", "WOOW:SYN480R", "SYN480R", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "C916347",
            {"1": "GND", "2": "RF_IN", "3": "+3V3", "4": None, "5": "RF_DATA", "6": "GND", "7": "RF_SQ", "8": "RF_XTAL"})
        add("Y1", "WOOW:Crystal_4P", "13.52127MHz 20pF", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", "C654957",
            {"1": "RF_XTAL", "3": "GND", "2": "GND", "4": "GND"})
        add("R14", "Device:R", "0R", R0603, "C21189", {"1": "RF_SQ", "2": "GND"})
        add("C17", "Device:C", "1uF", C0603, "C15849", {"1": "+3V3", "2": "GND"})
        add("C15", "Device:C", "2.7pF C0G" if v31 else "6.8pF C0G", C0603, "C162221" if v31 else "C1679",
            {"1": "RF_ANT", "2": "RF_IN"})
        add("C16", "Device:C", "2.7pF C0G" if v31 else "1.8pF C0G", C0603, "C162221" if v31 else "C1640",
            {"1": "RF_ANT", "2": "GND"})
        add("L3", "Device:L", "47nH" if v31 else "27nH", "Inductor_SMD:L_0603_1608Metric", "C29683" if v31 else "C12100",
            {"1": "RF_ANT", "2": "GND"})
        add("L4", "Device:L", "33nH" if v31 else "47nH", "Inductor_SMD:L_0603_1608Metric", "C35050" if v31 else "C29683",
            {"1": "RF_IN", "2": "GND"})
        add("ANT1", "Connector:Conn_01x01_Pin", "433MHz 1/4λ 17.3cm", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", "",
            {"1": "RF_ANT"})

        # ---------------- 使用者介面 ----------------
        if v34:   # 原 6×6 按鍵放不下（上緣讓給 J5）：改 3.9×3.0（基礎料），放在 J5 後方＝開蓋才按得到的維修鍵
            add("B1", "Switch:SW_Push", "TS-1088-AR02016", "WOOW:SW_SPST_TS-1088", "C720477", {"1": "GND", "2": "BOOT"})
        else:
            add("B1", "Switch:SW_Push", "ZX-QC66-4.3TP", "WOOW:SW_Push_6x6mm_SMD_ZX-QC66", "C7470150", {"1": "GND", "2": "BOOT"})
        if not v32:
            add("S1", "Switch:SW_DIP_x02", "DIP 2P", "Button_Switch_SMD:SW_DIP_SPSTx02_Slide_Copal_CHS-02B_W7.62mm_P1.27mm", "C7421516",
                {"1": "Mode_bit1", "2": "Mode_bit0", "3": "GND", "4": "GND"})
        add("P2", "Connector_Generic:Conn_02x03_Odd_Even", "PROG",
            "WOOW:ProgPads_2x03_P2.54mm" if v34 else "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical", "",
            {"1": "+3V3", "2": "GND", "3": "U0TXD", "4": "U0RXD", "5": "EN", "6": "BOOT"})   # 1×6 直排會壓到 H3
    else:
        # ---------------- STM32F103C8T6 ----------------
        stm = {str(n): None for n in range(1, 49)}
        stm.update({"1": "+3V3", "9": "+3V3", "24": "+3V3", "36": "+3V3", "48": "+3V3",
                    "8": "GND", "23": "GND", "35": "GND", "47": "GND",
                    "7": "NRST", "44": "BOOT0", "20": "BOOT1",
                    "10": "Output_1", "11": "Output_2", "12": "Output_3", "13": "Output_4",
                    "32": "Mode_bit1", "33": "Mode_bit0", "34": "SWDIO", "37": "SWCLK", "38": "RF_4",
                    "39": "RF_3", "40": "RF_2", "41": "RF_1",
                    "42": "Relay_4", "43": "Relay_3", "45": "Relay_2", "46": "Relay_1",
                    "22": "Reset_Button", "25": "Input_4", "26": "Input_3", "27": "Input_2", "28": "Input_1"})
        if v2:
            stm.update({"30": "ZB_RX", "31": "ZB_TX", "18": "LED", "19": "ZB_RST"})  # PA9 USART1_TX, PA10 USART1_RX, PB0, PB1
        add("U1", "MCU_ST_STM32F1:STM32F103C8Tx", "STM32F103C8T6", "Package_QFP:LQFP-48_7x7mm_P0.5mm", "C8734", stm)
        for i, ref in enumerate(["C1", "C2", "C3", "C4"]):
            add(ref, "Device:C", "100nF", C0603, "C14663", {"1": "+3V3", "2": "GND"})
        add("R1", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "NRST"})
        add("C5", "Device:C", "100nF" if v2 else "1uF", C0603, "C14663" if v2 else "C15849", {"1": "NRST", "2": "GND"})
        add("R2", "Device:R", "100k", R0603, "C25803", {"1": "BOOT0", "2": "GND"})
        add("R3", "Device:R", "100k", R0603, "C25803", {"1": "BOOT1", "2": "GND"})
        if v2:
            add("C10", "Device:C", "1uF", C0603, "C15849", {"1": "+3V3", "2": "GND"})  # VDDA 旁路
            add("TP1", "Connector:TestPoint", "BOOT0", "TestPoint:TestPoint_Pad_D1.5mm", "", {"1": "BOOT0"})

        # ---------------- Tuya ZS3L ----------------
        zs = {"1": None, "2": None, "3": None, "4": "Output_4", "5": "Output_3", "6": "Output_2", "7": "Output_1",
              "8": "+3V3", "9": "GND", "10": "Input_1", "11": "Input_2", "12": "Input_3", "13": "Input_4",
              "14": None, "15": None, "16": "NET_LED"}
        if v2:
            zs.update({"1": "ZB_RST", "15": "ZB_RX", "16": "ZB_TX"})
            add("R13", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "ZB_RST"})
        add("U3", "WOOW:ZS3L", "ZS3L", "WOOW:Tuya_ZS3L", "", zs)
        add("C8", "Device:C", "10uF", C0603, "C19702", {"1": "+3V3", "2": "GND"})
        add("C9", "Device:C", "100nF", C0603, "C14663", {"1": "+3V3", "2": "GND"})
        led_net = "LED" if v2 else "NET_LED"
        add("R4", "Device:R", "100R", R0603, "C22775", {"1": led_net, "2": "LED_A"})
        add("L1", "Device:LED", "Blue", "LED_SMD:LED_0603_1608Metric", "C965807", {"2": "LED_A", "1": "GND"})

        # ---------------- 使用者介面 ----------------
        add("B1", "Switch:SW_Push", "ZX-QC66-4.3TP", "WOOW:SW_Push_6x6mm_SMD_ZX-QC66", "C7470150", {"1": "GND", "2": "Reset_Button"})
        add("S1", "Switch:SW_DIP_x02", "DIP 2P", "Button_Switch_SMD:SW_DIP_SPSTx02_Slide_Copal_CHS-02B_W7.62mm_P1.27mm", "C7421516",
            {"1": "Mode_bit1", "2": "Mode_bit0", "3": "GND", "4": "GND"})
        add("P2", "Connector_Generic:Conn_01x04", "SWD", "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical", "",
            {"1": "+3V3", "2": "SWCLK", "3": "SWDIO", "4": "GND"})
        add("RF1", "WOOW:EPA09-4D", "EPA09-4D", "WOOW:Ebelong_EPA09-4D", "",
            {"1": "RF_1", "2": "RF_2", "3": "RF_3", "4": "RF_4", "5": "+3V3", "6": "GND"})

    # ---------------- V3.2：4 路 DI（乾接點／5–24VDC，光耦隔離）----------------
    if v32:
        # J5：+12V（經 R23 1.5k 限流：短路 8mA／0.096W，1206 0.25W 可長期短路；4 路全接通每路仍有 1.16mA）｜IN1–IN4｜COM｜GND
        #   乾接點：+12V →接點→ INx，COM 接 GND（端子上跳線）。PLC／外部 DC：INx 與 COM 之間 5–24V，NPN、PNP 都可
        #   （TLP290-4 是交流輸入光耦，LED 反向並聯，不分極性）。R15–R18 3.3k：5V 時 1.15mA、24V 時 6.9mA／0.16W（1206 0.25W）
        #   輸出：集極接 ESP32 GPIO（韌體開內建上拉 ~45k）＋10nF 到地（τ≈0.45ms，其餘由韌體防彈跳），接通＝低電位。
        #   不用外部上拉：光耦導通只需吸 ~73µA，CTR 綽綽有餘；10nF 讓這條約 5cm 的線在高頻是低阻抗、不易拾取雜訊
        # V3.4：原板框內和 P1（市電）共用上方開窗，要離 P1 6.4mm → 3.5mm 7P 放不下，改 2.54mm（KF2EDGR-2.54-7P）
        add("J5", "Connector_Generic:Conn_01x07", "DI 12V/IN1-4/COM/GND",
            "WOOW:TerminalBlock_Pluggable_1x07_P2.54mm_Horizontal" if v34 else "WOOW:TerminalBlock_Pluggable_1x07_P3.50mm_Horizontal",
            "C577599" if v34 else "",
            {"1": "+12V_DI", "2": "DI_IN1", "3": "DI_IN2", "4": "DI_IN3", "5": "DI_IN4", "6": "DI_COM", "7": "GND"})
        add("R23", "Device:R", "1.5k", "Resistor_SMD:R_1206_3216Metric", "C26030", {"1": "+12V", "2": "+12V_DI"})
        opto = {}
        for k in range(1, 5):
            add(f"R{14 + k}", "Device:R", "3.3k", "Resistor_SMD:R_1206_3216Metric", "C26032", {"1": f"DI_IN{k}", "2": f"DI_A{k}"})
            add(f"C{17 + k}", "Device:C", "10nF", C0603, "C57112", {"1": f"DI_{k}", "2": "GND"})
            opto.update({str(2 * k - 1): f"DI_A{k}", str(2 * k): "DI_COM", str(18 - 2 * k): f"DI_{k}", str(17 - 2 * k): "GND"})
        add("U5", "WOOW:TLP290-4", "TLP290-4", "Package_SO:SOP-16_4.55x10.3mm_P1.27mm", "C39031", opto)

    # ---------------- V3.3：NFC 設定介面（ST25DV04KC＋板上印刷線圈）----------------
    if v33:
        # 手機（iOS Core NFC／Android，ISO 15693）讀寫 EEPROM 與 256B mailbox；ESP32 經 I2C 讀同一份設定。
        # 線圈 L1N 1.27µH（scripts/nfc_coil.py），28.5pF 內建＋C22 75pF → 13.88MHz（略高於 13.56MHz，留給外殼與手機靠近
        # 的下移）；C24 空位微調。GPO 不接：韌體每 50ms 輪詢 IT_STS_Dyn。V_EH 不用（預設關閉）。
        add("U6", "WOOW:ST25DV04KC", "ST25DV04KC-IE6S3", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "C3304276",
            {"1": None, "2": "NFC_AC0", "3": "NFC_AC1", "4": "GND", "5": "NFC_SDA", "6": "NFC_SCL", "7": None, "8": "+3V3"})
        if v34:   # 板子平躺在導軌盒底座、前蓋離板 >50mm：改用 FPC 天線貼前蓋內側，J6（JST SH 2P）接回來。
                  # 調諧電容依天線電感決定（docs/verification/V3.4.md 表），C22／C24 預設不上件
            add("J6", "Connector_Generic:Conn_01x02", "NFC ANT", "Connector_JST:JST_SH_BM02B-SRSS-TB_1x02-1MP_P1.00mm_Vertical",
                "C160388", {"1": "NFC_AC0", "2": "NFC_AC1"})   # MP 固定腳不接
            add("C22", "Device:C", "DNP tune", C0603, "", {"1": "NFC_AC0", "2": "NFC_AC1"})
        else:
            add("L5", "Device:L", "NFC coil 1.27uH", "WOOW:NFC_Coil_14.9x14.6mm_8T", "", {"1": "NFC_AC0", "2": "NFC_AC1"})
            add("C22", "Device:C", "75pF C0G", C0603, "C1681", {"1": "NFC_AC0", "2": "NFC_AC1"})
        add("C24", "Device:C", "DNP trim", C0603, "", {"1": "NFC_AC0", "2": "NFC_AC1"})
        add("C23", "Device:C", "100nF", C0603, "C14663", {"1": "+3V3", "2": "GND"})
        add("R24", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "NFC_SDA"})
        add("R25", "Device:R", "10k", R0603, "C25804", {"1": "+3V3", "2": "NFC_SCL"})

    # ---------------- 4 路繼電器 ----------------
    nc = {1: None, 2: None, 3: "DO_3_NC", 4: "DO_4_NC"}
    for k in range(1, 5):
        add(f"R{4 + k}", "Device:R", "1k", R0603, "C21190", {"1": f"Relay_{k}", "2": f"G{k}"})
        add(f"R{8 + k}", "Device:R", "10k", R0603, "C25804", {"1": f"G{k}", "2": "GND"})
        add(f"Q{k}", "Transistor_FET:DMG2302U", "Si2302CDS", "Package_TO_SOT_SMD:SOT-23", "C5224182",
            {"1": f"G{k}", "2": "GND", "3": f"COIL{k}"})
        # V3.1：續流改 Schottky B5819W（Vf 約 0.3V@20mA）。韌體 PWM 降壓保持時，Omron 驗證條件要 Vf ≤0.4V
        if v31:
            add(f"D{k}", "Device:D_Schottky", "B5819W", "Diode_SMD:D_SOD-123", "C8598", {"1": "+12V", "2": f"COIL{k}"})
        else:
            add(f"D{k}", "Diode:1N4148W", "1N4148W", "Diode_SMD:D_SOD-123", "C81598", {"1": "+12V", "2": f"COIL{k}"})
        add(f"K{k}", "Relay:G5Q-1", "G5Q-1 DC12", "WOOW:Relay_SPDT_Omron-G5Q-1_Tight", "C397244",
            {"1": "+12V", "5": f"COIL{k}", "2": "DO_COM", "3": f"DO_{k}_NO", "4": nc[k]})
    # V3.4：KEFA KF2EDGR-5.08-8P（C441210，規格書建議孔 Ø1.6），JLC 代焊
    add("P3", "Connector_Generic:Conn_01x08", "AC OUT", "WOOW:TerminalBlock_Pluggable_1x08_P5.08mm_Horizontal", "C441210" if v34 else "",
        {"1": "AC_L", "2": "DO_COM", "3": "DO_4_NC", "4": "DO_4_NO", "5": "DO_3_NC", "6": "DO_3_NO",
         "7": "DO_2_NO", "8": "DO_1_NO"})
    # ---------------- 固定孔（M3，非金屬化；位置照原板）----------------
    for i in range(1, 5):
        add(f"H{i}", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", "", {})
    return P


HOLES = {"H1": (-24.95, 16.53), "H2": (24.95, 66.56), "H3": (-24.95, 66.56), "H4": (24.95, 16.53)}

MAINS = {"AC_L_IN", "AC_L", "AC_N", "DO_COM", "DO_1_NO", "DO_2_NO", "DO_3_NO", "DO_4_NO", "DO_3_NC", "DO_4_NC"}
POWER_FLAGS = ["+12V", "+3V3", "GND", "AC_L", "AC_N", "AC_L_IN"]

CHANGES = {
    "3.4": [
        "板子改回原板框 63.4×83.08（外殼 4-02-3 導軌盒，固定孔 50×50）；V3.2／V3.3 加高的板子放不進",
        "J5 改 KF2EDGR-2.54-7P（C577599），和 P1 同排、離市電 ≥6.4mm",
        "NFC 板上線圈拿掉，改 J6（JST SH 2P，C160388）接 FPC 天線貼前蓋內側；C22／C24 依天線調諧",
        "P2 排針改背面 2×3 燒錄測試點；B1 改 TS-1088（C720477）放 J5 後方",
    ],
    "3.3": [
        "U6 ST25DV04KC（C3304276）＋L5 板上印刷線圈 14.9×14.6mm 8 圈 1.27µH＋C22 75pF（13.88MHz）＋C24 微調空位",
        "NFC I2C：SDA＝IO20、SCL＝IO19，各 10k 上拉；GPO 不接（韌體輪詢）。DI_1–4 改接 IO3／IO15／IO22／IO21（與 I2C 照左右順序排，不交叉）",
        "J5 改到左側板邊（DI 電線從左側出），左上角留給線圈；左半段高度 93.08→107.2mm",
    ],
    "3.2": [
        "J5 7P 3.5mm 插拔端子：+12V（R23 1.5k 限流，短路 8mA）／IN1–IN4／COM／GND；U5 TLP290-4 交流輸入光耦（NPN/PNP、乾接點都可）",
        "DI1–DI4 → IO22/IO21/IO20/IO19（ESP32 內建上拉＋10nF）；S1 指撥拿掉（IO10/IO11 空出），模式改軟體設定",
        "板子上緣加長 10mm 放 J5；市電區與繼電器區佈線不動",
    ],
    "3.1": [
        "433 匹配：C15 6.8p→2.7p、C16 1.8p→2.7p（C162221，±0.1pF）、L3 27n→47n（C29683）、L4 47n→33n（C35050）；拓撲與佈線不變",
        "D1–D4 1N4148W → B5819W Schottky（C8598，基礎料、同 SOD-123）：配合韌體 PWM 降壓保持，續流 Vf ≤0.4V",
        "電源預算：韌體全壓 100ms 後 PWM 60% 保持，每顆 12V 端 33.3→14.4mA（SPICE），4 顆全開最壞約 135mA < 167mA，取消 ≤3 顆限制",
    ],
    "3.0": [
        "U1 STM32F103C8T6 + U3 Tuya ZS3L → U1 ESP32-C6-WROOM-1-N8（C5366877，Zigbee 3.0 單晶片，JLC 可貼）",
        "RF1 Ebelong EPA09-4D（4 路已解碼、無規格書、JLC 無料）→ U3 SYN480R（C916347）+ Y1 13.52127MHz + LC 匹配，ESP32 韌體解 EV1527/PT2262",
        "ANT1 433MHz 1/4 波長天線焊點（17.3cm 線或彈簧天線，手焊）",
        "B1 改接 IO9（BOOT）：上電按住進下載模式，平時當配對鍵；P2 SWD → 6 腳燒錄座（3V3/EN/TX/RX/BOOT/GND）",
        "移除 STM32 周邊（C3、C4、C10、R13、TP1）與 ZS3L↔STM32 的 Input_*/Output_* 交握線",
        "電源預算：Zigbee TX +12dBm 峰值 185mA@3.3V ≈ 12V 側 58mA；繼電器 4 顆全吸 133mA → 合計 ~192mA 超過 IRM-02-12 額定 167mA，韌體限制同時吸合 ≤3 顆或 TX ≤0dBm（見 README）",
    ],
    "2.0": [
        "F1 慢斷保險絲 T3.15A 串在 AC_L 入口（同時保護 IRM 與 P3 pin1 供給 DO_COM 的風扇迴路）",
        "RV1 壓敏電阻 07D471K 跨 L-N（突波）",
        "電源預算：3.3V 改降壓後 12V 側只剩約 13mA，4 顆繼電器全吸 133mA，合計約 146mA < IRM-02-12 額定 167mA（保留原電源與位置）",
        "U4 LM1117 線性 -> AP63203WU 同步降壓：免散熱、陶瓷電容穩定、輸入耐 32V（IRM OVP 16.2V 也安全）",
        "ZS3L UART 接 STM32 USART1：TXD->PA10、RXD<-PA9；RST<-PB1 (10k 上拉)；指示燈改由 PB0 驅動",
        "NRST 電容 1uF -> 100nF；VDDA 加 1uF；BOOT0 加測試點 TP1",
        "P1/P3 端子與 4 個固定孔位置不變；市電走廊沿上緣＋右緣，IRM 的 AC 兩腳 L/N 對調以避開 K4 線圈",
        "RF1、S1（改背面）、P2、繼電器驅動元件移出市電 6.4mm 安全帶",
        "PCB：市電區與低壓區 >=6.4mm 隔離＋開槽、市電網路間 >=1.5mm、固定孔改非金屬化、鋪銅離板邊 0.5mm",
    ]
}
