"""由 design.py 產生 KiCad 原理圖（標籤式連線：每個腳位接一個網路標籤）。
用法：python gen_schematic.py 2.0  -> hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_sch
"""
import os
import sys
import datetime
import kicad_sch_api as ksa
sys.path.insert(0, os.path.dirname(__file__))
import design

VERSION = sys.argv[1] if len(sys.argv) > 1 else "2.0"
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PRJ = os.path.join(ROOT, "hardware", "WO30109_FreshAir")
ksa.get_symbol_cache().add_library_path(os.path.join(ROOT, "hardware", "lib", "WOOW.kicad_sym"))

G = 2.54
# 版面：各功能區塊在 A2 圖紙上的起點（mm）與每列寬度
BLOCKS = [
    ("市電輸入／電源", ["P1", "F1", "RV1", "U2", "C6", "C14", "U4", "C12", "L2", "C7", "C13"], (25, 40)),
    ("MCU", ["U1", "C1", "C2", "C3", "C4", "C10", "R1", "C5", "R2", "R3", "TP1", "P2", "H1", "H2", "H3", "H4"], (25, 140)),
    ("Zigbee / RF / 介面", ["U3", "R13", "C8", "C9", "R4", "L1", "RF1", "B1", "S1",
                           "Y1", "R14", "C17", "C15", "C16", "L3", "L4", "ANT1"], (240, 40)),
    ("繼電器", [r for k in range(1, 5) for r in (f"R{4+k}", f"R{8+k}", f"Q{k}", f"D{k}", f"K{k}")] + ["P3"], (240, 170)),
    ("DI 輸入（光耦隔離）", ["J5", "R23", "U5"] + [f"R{14+k}" for k in range(1, 5)]
     + [f"C{17+k}" for k in range(1, 5)], (25, 300)),
]
BIG = {"U1": (55, 75) if VERSION < "3" else (45, 55), "U2": (45, 25), "U3": (55, 45), "RF1": (40, 35), "P3": (35, 40), "U4": (40, 25),
       "U5": (35, 35), "J5": (30, 30)}


def snap(v):
    return round(v / G) * G


def main():
    P = design.build(VERSION)
    sch = ksa.create_schematic("WO30109_FreshAir")
    sch.set_paper_size("A2")
    sch.set_title_block(title="WO_30109 Zigbee 新風控制器", rev=VERSION,
                        date=datetime.date.today().isoformat(), company="WOOWTECH",
                        comments={1: "由 scripts/design.py 產生，請改 design.py 後重跑，勿直接手改",
                                  2: "V1.3 原設計重繪" if VERSION == "1.3" else f"V{VERSION} 改版：見 CHANGELOG.md"})
    placed = set()
    for title, refs, (bx, by) in BLOCKS:
        sch.add_text(title, position=(bx, by - 12), size=3)
        x, y, rowh = bx, by, 0
        for ref in refs:
            if ref not in P:
                continue
            w, h = BIG.get(ref, (22, 22))
            if x + w > bx + 200:
                x, y, rowh = bx, y + rowh + 8, 0
            part = P[ref]
            sch.components.add(lib_id=part["lib"], reference=ref, value=part["value"],
                               position=(snap(x + w / 2), snap(y + h / 2)), footprint=part["footprint"],
                               LCSC=part["lcsc"] or "")
            if ref.startswith("H") or ref.startswith("TP") or ref.startswith("ANT"):   # 固定孔、測試點、天線焊點：不進 BOM（與 PCB 一致）
                sch.components.get(ref).in_bom = False
            placed.add(ref)
            x += w + 8
            rowh = max(rowh, h)
    missing = set(P) - placed
    assert not missing, f"未排版：{missing}"
    # 每個腳位：有網路 -> 標籤；None -> no-connect
    n_lab = n_nc = 0
    for ref, part in P.items():
        for num, pt in sch.list_component_pins(ref):
            if num not in part["pins"]:
                raise SystemExit(f"{ref} pin {num} 未在 design.py 定義")
            net = part["pins"][num]
            if net is None:
                sch.no_connects.add(position=(pt.x, pt.y))
                n_nc += 1
            else:
                sch.add_label(net, position=(pt.x, pt.y))
                n_lab += 1
    # PWR_FLAG：只加在「有 power_in 腳、卻沒有 power_out 腳驅動」的網路
    types = {}
    for ref in P:
        comp = sch.components.get(ref)
        for pin in comp.list_pins():
            net = P[ref]["pins"].get(str(pin["number"]))
            if net:
                types.setdefault(net, set()).add(pin.get("type") or pin.get("electrical_type"))
    need = sorted(n for n, t in types.items() if "power_in" in t and "power_out" not in t)
    need += sorted(n for n in design.POWER_FLAGS if n in types and n not in need and "power_out" not in types[n])
    for i, net in enumerate(dict.fromkeys(need)):
        x, y = 30 + i * 15, 20
        sch.components.add(lib_id="power:PWR_FLAG", reference=f"#FLG{i+1:02d}", value="PWR_FLAG", position=(x, y))
        for num, pt in sch.list_component_pins(f"#FLG{i+1:02d}"):
            sch.add_label(net, position=(pt.x, pt.y))
    print("PWR_FLAG:", list(dict.fromkeys(need)))
    os.makedirs(PRJ, exist_ok=True)
    out = os.path.join(PRJ, "WO30109_FreshAir.kicad_sch")
    sch.save(out)
    print(f"V{VERSION}: {len(P)} 顆零件, {n_lab} 標籤, {n_nc} no-connect -> {out}")


if __name__ == "__main__":
    main()
