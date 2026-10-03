"""把 V3.0 新增的自訂符號寫進 hardware/lib/WOOW.kicad_sym（重跑會覆蓋同名符號，其餘符號不動）。

腳位來源：
  ESP32-C6-WROOM-1  Espressif 規格書 v1.4 Table 3-1（29 腳，EPAD=29）
  SYN480R           JSMSEMI SYN480R 規格書「管腳定義」（SOP-8）
"""
import os, re

LIB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hardware", "lib", "WOOW.kicad_sym")
F = "(effects (font (size 1.27 1.27)))"


def prop(name, val, y, hide=False):
    h = " (hide yes)" if hide else ""
    return f'\t\t(property "{name}" "{val}" (at 0 {y} 0){h} {F})\n'


def pin(kind, x, y, rot, name, num):
    return f'\t\t\t(pin {kind} line (at {x} {y} {rot}) (length 2.54) (name "{name}" {F}) (number "{num}" {F}))\n'


def symbol(name, ref, fp, desc, box, pins):
    x0, y0, x1, y1 = box
    s = f'\t(symbol "{name}"\n\t\t(exclude_from_sim no) (in_bom yes) (on_board yes)\n'
    s += prop("Reference", ref, y0 + 2.54) + prop("Value", name, y1 - 2.54)
    s += prop("Footprint", fp, 0, True) + prop("Datasheet", "", 0, True) + prop("Description", desc, 0, True)
    s += f'\t\t(symbol "{name}_0_1"\n\t\t\t(rectangle (start {x0} {y0}) (end {x1} {y1}) (stroke (width 0.254) (type default)) (fill (type background)))\n\t\t)\n'
    s += f'\t\t(symbol "{name}_1_1"\n' + "".join(pin(*p) for p in pins) + "\t\t)\n\t\t(embedded_fonts no)\n\t)\n"
    return s


def esp32c6():
    L, R = -15.24, 15.24
    left = [("power_in", "3V3", 2), ("input", "EN", 3), ("bidirectional", "IO4", 4), ("bidirectional", "IO5", 5),
            ("bidirectional", "IO6", 6), ("bidirectional", "IO7", 7), ("bidirectional", "IO0", 8),
            ("bidirectional", "IO1", 9), ("bidirectional", "IO8", 10), ("bidirectional", "IO10", 11),
            ("bidirectional", "IO11", 12), ("bidirectional", "IO12/USB_D-", 13), ("bidirectional", "IO13/USB_D+", 14)]
    right = [("bidirectional", "IO9", 15), ("bidirectional", "IO18", 16), ("bidirectional", "IO19", 17),
             ("bidirectional", "IO20", 18), ("bidirectional", "IO21", 19), ("bidirectional", "IO22", 20),
             ("bidirectional", "IO23", 21), ("no_connect", "NC", 22), ("bidirectional", "IO15", 23),
             ("bidirectional", "RXD0/IO17", 24), ("bidirectional", "TXD0/IO16", 25), ("bidirectional", "IO3", 26),
             ("bidirectional", "IO2", 27)]
    pins = [(k, L - 2.54, 15.24 - i * 2.54, 0, n, num) for i, (k, n, num) in enumerate(left)]
    pins += [(k, R + 2.54, 15.24 - i * 2.54, 180, n, num) for i, (k, n, num) in enumerate(right)]
    pins += [("power_in", -2.54, -22.86, 90, "GND", 1), ("passive", 0, -22.86, 90, "GND", 28),
             ("passive", 2.54, -22.86, 90, "GND", 29)]
    return symbol("ESP32-C6-WROOM-1", "U", "WOOW:Espressif_ESP32-C6-WROOM-1",
                  "Espressif ESP32-C6 module: Wi-Fi 6, BLE 5, IEEE 802.15.4 (Zigbee 3.0/Thread), PCB antenna",
                  (L, 17.78, R, -20.32), pins)


def syn480r():
    pins = [("power_in", -10.16, 3.81, 0, "GND", 1), ("input", -10.16, 1.27, 0, "ANT", 2),
            ("power_in", -10.16, -1.27, 0, "VDD", 3), ("no_connect", -10.16, -3.81, 0, "NC", 4),
            ("output", 10.16, -3.81, 180, "DO", 5), ("input", 10.16, -1.27, 180, "SHUT", 6),
            ("input", 10.16, 1.27, 180, "SQ", 7), ("passive", 10.16, 3.81, 180, "RO", 8)]
    return symbol("SYN480R", "U", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
                  "JSMSEMI SYN480R 300-440MHz ASK/OOK receiver, SOP-8", (-7.62, 6.35, 7.62, -6.35), pins)


def crystal4():
    # KiCad 內建 Device:Crystal_GND24 經 kicad-sch-api 寫出後 KiCad 讀不回來，另做一個等效符號
    pins = [("passive", -7.62, 0, 0, "X1", 1), ("passive", 7.62, 0, 180, "X2", 3),
            ("passive", 0, -5.08, 90, "GND", 2), ("passive", 0, 5.08, 270, "GND", 4)]
    return symbol("Crystal_4P", "Y", "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm",
                  "Crystal, 4-pad SMD (pins 1/3 crystal, 2/4 case ground)", (-5.08, 2.54, 5.08, -2.54), pins)


def main():
    s = open(LIB).read()
    for name, body in (("ESP32-C6-WROOM-1", esp32c6()), ("SYN480R", syn480r()), ("Crystal_4P", crystal4())):
        m = re.search(rf'\n\t\(symbol "{re.escape(name)}"\n.*?\n\t\)\n', s, re.S)
        if m:
            s = s[:m.start() + 1] + body + s[m.end():]
        else:
            i = s.rindex("\n)")
            s = s[:i + 1] + body + s[i + 1:]
    open(LIB, "w").write(s)
    print("symbols:", re.findall(r'\n\t\(symbol "([^"]+)"', s))


if __name__ == "__main__":
    main()
