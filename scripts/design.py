"""WO_30109 Zigbee 新風控制器 — 電路的唯一真實來源（single source of truth）。

每顆零件：ref -> (symbol lib_id, value, footprint, LCSC, {pin: net})
  * net 為 None 表示不接（會在原理圖打 no-connect）。
  * VERSION="1.3"：照原設計 V1.3（2025-08-28 PDF 原理圖）重繪，不改任何電路。
  * VERSION="2.0"：改版，差異寫在 CHANGES。
  * VERSION="3.0"：STM32＋Tuya ZS3L＋EPA09-4D → ESP32-C6 單晶片（Zigbee 3.0）＋SYN480R 433MHz 接收，差異寫在 CHANGES。
"""
R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"


def build(version="2.0"):
    P = {}

    def add(ref, lib, val, fp, lcsc, pins):
        P[ref] = dict(lib=lib, value=val, footprint=fp, lcsc=lcsc, pins=pins)

    v3 = version.startswith("3")
    v2 = version.startswith("2") or v3          # V3.0 沿用 V2.0 的市電、電源、繼電器

    # ---------------- 市電輸入 ----------------
    if v2:
        add("P1", "Connector_Generic:Conn_01x02", "AC IN 230V", "WOOW:TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal", "",
            {"1": "AC_L_IN", "2": "AC_N"})
        add("F1", "Device:Fuse", "T3.15A 250V", "Fuse:Fuseholder_TR5_Littelfuse_No560_No460", "",
            {"1": "AC_L_IN", "2": "AC_L"})
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
        # SYN480R 433.92MHz ASK/OOK 接收（規格書典型應用，433.92MHz 欄）：
        #   ANT1 ─┬─ C16 1.8p ─ GND ─┬─ L3 27nH ─ GND，經 C15 6.8p 串到 ANT 腳；ANT 腳 L4 47nH 到 GND
        #   Y1 13.52127MHz/20pF 接 RO-GND；SHUT 接地（常開）；SQ 經 R14 0R 接地（關靜噪，靈敏度 +3dB）
        add("U3", "WOOW:SYN480R", "SYN480R", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "C916347",
            {"1": "GND", "2": "RF_IN", "3": "+3V3", "4": None, "5": "RF_DATA", "6": "GND", "7": "RF_SQ", "8": "RF_XTAL"})
        add("Y1", "WOOW:Crystal_4P", "13.52127MHz 20pF", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", "C654957",
            {"1": "RF_XTAL", "3": "GND", "2": "GND", "4": "GND"})
        add("R14", "Device:R", "0R", R0603, "C21189", {"1": "RF_SQ", "2": "GND"})
        add("C17", "Device:C", "1uF", C0603, "C15849", {"1": "+3V3", "2": "GND"})
        add("C15", "Device:C", "6.8pF C0G", C0603, "C1679", {"1": "RF_ANT", "2": "RF_IN"})
        add("C16", "Device:C", "1.8pF C0G", C0603, "C1640", {"1": "RF_ANT", "2": "GND"})
        add("L3", "Device:L", "27nH", "Inductor_SMD:L_0603_1608Metric", "C12100", {"1": "RF_ANT", "2": "GND"})
        add("L4", "Device:L", "47nH", "Inductor_SMD:L_0603_1608Metric", "C29683", {"1": "RF_IN", "2": "GND"})
        add("ANT1", "Connector:Conn_01x01_Pin", "433MHz 1/4λ 17.3cm", "TestPoint:TestPoint_THTPad_D2.0mm_Drill1.0mm", "",
            {"1": "RF_ANT"})

        # ---------------- 使用者介面 ----------------
        add("B1", "Switch:SW_Push", "ZX-QC66-4.3TP", "WOOW:SW_Push_6x6mm_SMD_ZX-QC66", "C7470150", {"1": "GND", "2": "BOOT"})
        add("S1", "Switch:SW_DIP_x02", "DIP 2P", "Button_Switch_SMD:SW_DIP_SPSTx02_Slide_Copal_CHS-02B_W7.62mm_P1.27mm", "C7421516",
            {"1": "Mode_bit1", "2": "Mode_bit0", "3": "GND", "4": "GND"})
        add("P2", "Connector_Generic:Conn_02x03_Odd_Even", "PROG", "Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical", "",
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

    # ---------------- 4 路繼電器 ----------------
    nc = {1: None, 2: None, 3: "DO_3_NC", 4: "DO_4_NC"}
    for k in range(1, 5):
        add(f"R{4 + k}", "Device:R", "1k", R0603, "C21190", {"1": f"Relay_{k}", "2": f"G{k}"})
        add(f"R{8 + k}", "Device:R", "10k", R0603, "C25804", {"1": f"G{k}", "2": "GND"})
        add(f"Q{k}", "Transistor_FET:DMG2302U", "Si2302CDS", "Package_TO_SOT_SMD:SOT-23", "C5224182",
            {"1": f"G{k}", "2": "GND", "3": f"COIL{k}"})
        add(f"D{k}", "Diode:1N4148W", "1N4148W", "Diode_SMD:D_SOD-123", "C81598", {"1": "+12V", "2": f"COIL{k}"})
        add(f"K{k}", "Relay:G5Q-1", "G5Q-1 DC12", "WOOW:Relay_SPDT_Omron-G5Q-1_Tight", "C397244",
            {"1": "+12V", "5": f"COIL{k}", "2": "DO_COM", "3": f"DO_{k}_NO", "4": nc[k]})
    add("P3", "Connector_Generic:Conn_01x08", "AC OUT", "WOOW:TerminalBlock_Pluggable_1x08_P5.08mm_Horizontal", "",
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
