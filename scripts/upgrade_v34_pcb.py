"""V3.1 PCB -> V3.4 PCB：照外殼（精鋼塑模 4-02-3，35mm 導軌盒 88×72×59，固定孔 50×50）放回原板框 63.4×83.08。
加 4 路光耦 DI（J5 2.54mm 7P 和 P1 共用上方開窗）、NFC（U6＋J6 接前蓋內側的 FPC 天線）；P2 排針改背面測試點、B1 改小。

  python scripts/gen_schematic.py 3.4      # 先產生原理圖（kicad-sch-api）
  bash scripts/build_v34.sh                # 一鍵重建：本檔（換件、擺件、套網表、拆低壓走線）→ Freerouting → 補線 → 鋪銅 → 縫合

市電走線（鎖定）、繼電器、電源、端子、固定孔都不動；低壓走線全部拆掉重佈（同 V3.0 的做法）。
座標：這裡直接用 KiCad 絕對座標（mm，原點左上、Y 向下；板框 x 100–163.4、y 100–183.08），比 Gerber 座標好對照 DRC 報告。
每一步各開一個 Python 行程（KiCad 10 的 SWIG 在同一行程 Remove/Add 封裝後會回傳無型別物件）。
"""
import os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design
from upgrade_v20_pcb import V, set_lcsc, sch_netlist, ROOT, PRJ
from upgrade_v30_pcb import load_fp

OUT = os.environ.get("OUT_PCB", os.path.join(PRJ, "WO30109_FreshAir.kicad_pcb"))
BASE_REF = os.environ.get("BASE_REF", "f64c97b")     # V3.1 PCB（原板框；commit「hw: V3.1 433 匹配重新選值…」）
REL = "hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_pcb"
MAINS = lambda n: n.startswith("/AC_") or n.startswith("/DO_")
GONE = ["S1", "P2", "B1"]                              # 拿掉（P2、B1 換封裝，重新加）
NEW = ["J5", "U5", "R23", "P2", "B1", "U6", "J6", "C22", "C24", "C23", "R24", "R25"] \
    + [f"R{14 + k}" for k in range(1, 5)] + [f"C{17 + k}" for k in range(1, 5)]

# (x, y, 旋轉, 背面?)，KiCad 絕對座標。依據：
#   上緣 x 100–121.3 是低壓可用的開窗（右邊要離 P1 市電焊盤 ≥6.4mm）；J5 本體 19.58mm 吃掉 x 100.3–119.9、y 100–108.1
#   J5 焊腳排（y 106.9）把背面上緣隔成只有右端能出入的口袋（y 100.5–105.8）→ 留空給 J5 的 DI 走線
#   固定孔 H3（106.75, 116.52）的 courtyard 只有正面：背面零件自行保持在孔中心 3.5mm 外（固定柱）
#   背面的市電：P1 第 2 腳（128.98, 108.15）與 AC_N 背面走線（x 128.98）→ 背面低壓銅 x ≤ 122.08
PLACE = {
    "J5": (110.1, 106.9, 0, False),      # 原點＝Pin4；開口朝上緣
    "L1": (120.9, 101.9, 90, False),     # 狀態燈：開窗裡 J5 旁邊看得到
    "R4": (120.9, 104.9, 90, False),
    "B1": (111.8, 110.6, 0, False),      # J5 後方：開蓋才按得到（學遙控器、恢復出廠可改用 NFC App）
    "J6": (118.8, 111.1, 0, False),      # NFC 天線接頭（JST SH 直立）：線往上接前蓋內側的 FPC 天線
    "R23": (101.8, 110.8, 90, False),    # +12V 限流（J5 Pin1）
    # NFC：U6 與它的電容、I2C 上拉放背面、ESP32 下方（和 P2 一起）。第一次擺在 J5 後方的上緣口袋：口袋只有右端能出入，
    # U6 左側的天線與 I2C 訊號繞不出去（Freerouting 與 A* 都卡住）
    "U6": (119.0, 130.8, 90, True),
    "C23": (119.0, 135.6, 0, True),      # U6 VCC 去耦
    "C22": (115.9, 135.6, 0, True),      # 調諧（依天線電感，預設不上件）
    "C24": (112.8, 135.6, 0, True),
    "U5": (114.9, 113.9, 0, True),       # TLP290-4：背面、U3 正下方；背面旋轉 0°＝Pin1–8（LED）朝左、9–16（集極）朝右
    "R15": (101.8, 110.7, 90, True), "R16": (104.3, 110.7, 90, True),
    "R17": (106.8, 110.7, 90, True), "R18": (109.3, 110.7, 90, True),
    # DI 濾波 10nF：背面、ESP32 上排正下方（輸入腳旁濾波效果最好）。不放 U5 右邊：那裡是左上角往下接 ESP32
    # 唯一的背面通道（x 119–122），放了電容 NFC_SCL 與 DI 長線會佈不通（第一次建置 4 組 Freerouting 都卡在這）
    "C18": (112.0, 122.5, 90, True), "C19": (113.6, 122.5, 90, True),
    "C20": (115.2, 122.5, 90, True), "C21": (116.8, 122.5, 90, True),
    "P2": (112.8, 130.8, 0, True),       # 燒錄測試點：背面、ESP32 下方（UART0／EN／BOOT 都在附近）
    "R24": (110.3, 135.9, 90, True), "R25": (121.3, 136.6, 90, True),   # I2C 上拉
}


def place(fp, x, y, rot, bottom):
    if fp.IsFlipped() != bottom:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(V(x, y))


def step_place():
    P = design.build("3.4")
    lib_fp = {ref: load_fp(P[ref]["footprint"]) for ref in NEW}     # 封裝要在 LoadBoard 前載好
    base = subprocess.run(["git", "-C", ROOT, "show", f"{BASE_REF}:{REL}"], check=True, capture_output=True).stdout
    open(OUT, "wb").write(base)
    b = pcbnew.LoadBoard(OUT)
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}
    for ref in GONE:
        b.Remove(fps.pop(ref))
    for ref in NEW:
        fp = lib_fp[ref]; fp.SetReference(ref); fp.SetValue(P[ref]["value"]); set_lcsc(fp, P[ref]["lcsc"] or "")
        b.Add(fp); fps[ref] = fp
    for ref, (x, y, rot, bottom) in PLACE.items():
        place(fps[ref], x, y, rot, bottom)
    for ref in ("P2", "C22", "C24"):                                 # 測試點、不上件的調諧電容
        fps[ref].SetExcludedFromPosFiles(True)
    for ref in ("P2", "C22", "C24"):
        fps[ref].SetExcludedFromBOM(True)
    pcbnew.SaveBoard(OUT, b)


def step_nets():
    """套網表、拆掉所有低壓走線與鋪銅（市電已鎖定的保留），之後交給 Freerouting"""
    b = pcbnew.LoadBoard(OUT)
    SN = sch_netlist()

    def NF(name):
        ni = b.FindNet(name)
        if ni is None:
            ni = pcbnew.NETINFO_ITEM(b, name); b.Add(ni)
        return ni
    no_net = b.FindNet(0)
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            full = SN.get((fp.GetReference(), pad.GetNumber()))
            pad.SetNet(NF(full) if full else no_net)
    zones = [z for z in b.Zones() if not z.GetIsRuleArea()]
    tracks = [t for t in b.GetTracks() if not MAINS(t.GetNetname())]
    for item in zones + tracks:
        b.Remove(item)
    tb = b.GetTitleBlock(); tb.SetRevision("3.4"); b.SetTitleBlock(tb)   # 換件那個行程裡設會拿到無型別物件
    pcbnew.SaveBoard(OUT, b)
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}
    u5 = {p.GetNumber(): (round(pcbnew.ToMM(p.GetPosition().x), 2), round(pcbnew.ToMM(p.GetPosition().y), 2)) for p in fps["U5"].Pads()}
    print(f"  拆除低壓走線 {len(tracks)} 段、鋪銅 {len(zones)} 塊；U5 Pin1 {u5['1']}、Pin16 {u5['16']}")


STEPS = {"place": step_place, "nets": step_nets}

if __name__ == "__main__":
    if len(sys.argv) > 1:
        STEPS[sys.argv[1]]()
    else:
        for s in ("place", "nets"):
            print(f"== {s}")
            r = subprocess.run([sys.executable, __file__, s])
            if r.returncode: sys.exit(r.returncode)
