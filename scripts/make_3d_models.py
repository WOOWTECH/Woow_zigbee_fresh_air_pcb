"""自訂封裝的簡化 3D 模型（STEP）：本體方塊＋插件腳。給外殼設計檢查干涉、高度用，不是外觀模型。
需要 OCP（pip install cadquery-ocp）。封裝幾何由 KiCad 讀出（fpgeom.json，見 README「3D」一節）。
座標：KiCad 3D 模型 Y 軸向上，封裝 Y 軸向下 → 模型 y = -封裝 y。

本體高度來源：
  G5Q-1          15.3mm  Omron G5Q 規格書外形 20.5×10.1×15.3
  ZS3L            3.3mm  Tuya ZS3L 模組（含屏蔽罩，估計值）
  EPA09-4D        6.0mm  無公開規格書（估計值）
  插拔端子座      9.0mm  5.08/5.00mm 直角公座本體（不含插頭；插頭插上約 +6mm）
  6×6 按鍵        4.3mm  ZX-QC66-4.3TP 型號即高度
"""
import json, os, sys
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCP.BRep import BRep_Builder
from OCP.TopoDS import TopoDS_Compound
from OCP.gp import gp_Pnt, gp_Ax2, gp_Dir
from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
from OCP.IFSelect import IFSelect_RetDone

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "hardware", "lib", "WOOW.3dshapes")
HEIGHT = {
    "Relay_SPDT_Omron-G5Q-1_Tight": 15.3,
    "Tuya_ZS3L": 3.3,
    "Ebelong_EPA09-4D": 6.0,
    "TerminalBlock_Pluggable_1x02_P5.00mm_Horizontal": 9.0,
    "TerminalBlock_Pluggable_1x08_P5.08mm_Horizontal": 9.0,
    "SW_Push_6x6mm_SMD_ZX-QC66": 4.3,
}


def build(geom):
    b = BRep_Builder(); comp = TopoDS_Compound(); b.MakeCompound(comp)
    x0, y0, x1, y1 = geom["fab"]
    # 本體：封裝 y 往下為正 → 模型 y 取負
    box = BRepPrimAPI_MakeBox(gp_Pnt(x0, -y1, 0.0), x1 - x0, y1 - y0, geom["h"]).Shape()
    b.Add(comp, box)
    for px, py, th in geom["pads"]:
        if th:   # 插件腳：板下伸出 3mm
            pin = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(px, -py, -3.0), gp_Dir(0, 0, 1)), 0.4, 3.0).Shape()
            b.Add(comp, pin)
    return comp


def main(geom_json):
    os.makedirs(OUT, exist_ok=True)
    G = json.load(open(geom_json))
    for name, g in G.items():
        g["h"] = HEIGHT[name]
        w = STEPControl_Writer()
        w.Transfer(build(g), STEPControl_AsIs)
        path = os.path.join(OUT, name + ".step")
        assert w.Write(path) == IFSelect_RetDone, path
        print(f"{name}.step  本體 {g['fab'][2]-g['fab'][0]:.1f}×{g['fab'][3]-g['fab'][1]:.1f}×{g['h']}mm")


if __name__ == "__main__":
    main(sys.argv[1])
