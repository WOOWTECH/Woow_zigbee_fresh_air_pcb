"""V2.0 PCB -> V3.0 PCB（第 1 段：換零件、換網表、擺件、清低壓走線）。之後同 V2.0 交給 route_v20.py（Freerouting）。

V3.0 只動低壓區：STM32＋ZS3L＋EPA09-4D 換成 ESP32-C6-WROOM-1＋SYN480R。市電走線、繼電器、電源、端子、固定孔
全部沿用 V2.0（從 git 標籤 v2.0 的 PCB 開始，不需要原始 Gerber）。

  python3 scripts/upgrade_v30_pcb.py          # 需先 python scripts/gen_schematic.py 3.0

座標一律用「原板 Gerber 座標」(mm，原點=板底邊中點，Y 向上)，與 upgrade_v20_pcb.py 相同。
"""
import os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design
from upgrade_v20_pcb import K, V, place, pad_g, set_lcsc, sch_netlist, ROOT, PRJ

OUT = os.environ.get("OUT_PCB", os.path.join(PRJ, "WO30109_FreshAir.kicad_pcb"))
BASE_REF = os.environ.get("BASE_REF", "v2.0")
REL = "hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_pcb"
MAINS = lambda n: n.startswith("/AC_") or n.startswith("/DO_")


def load_fp(fpid):
    """從 WOOW.pretty 或 KiCad 標準庫載入封裝"""
    lib, name = fpid.split(":")
    path = os.path.join(ROOT, "hardware", "lib", "WOOW.pretty") if lib == "WOOW" else f"/usr/share/kicad/footprints/{lib}.pretty"
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None: raise SystemExit(f"找不到封裝 {fpid}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name)); return fp


def main():
    P = design.build("3.0")
    # 封裝要在 LoadBoard 之前全部載好：之後再呼叫 FootprintLoad 會拿到無型別的 SwigPyObject
    lib_fp = {ref: load_fp(part["footprint"]) for ref, part in P.items()}
    base = subprocess.run(["git", "-C", ROOT, "show", f"{BASE_REF}:{REL}"], check=True, capture_output=True).stdout
    open(OUT, "wb").write(base)
    board = pcbnew.LoadBoard(OUT)

    # 1) 移除 V3.0 沒有的零件，以及封裝換掉的零件（U1、U3、P2 同位號但不同零件）
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    for ref, fp in list(fps.items()):
        if ref not in P or fp.GetFPIDAsString() != P[ref]["footprint"]:
            board.Remove(fp); del fps[ref]
    # 2) 新增零件
    for ref, part in P.items():
        if ref in fps:
            fps[ref].SetValue(part["value"]); set_lcsc(fps[ref], part["lcsc"] or ""); continue
        fp = lib_fp[ref]; fp.SetReference(ref); fp.SetValue(part["value"])
        set_lcsc(fp, part["lcsc"] or "")
        board.Add(fp); fps[ref] = fp

    # 3) 擺件
    # ESP32-C6：放回原 ZS3L 位置（左緣），天線朝 -X 貼板邊；轉 90° 後 pin1–14 在下排（靠繼電器），15–28 在上排
    place(fps["U1"], center=(-31.7 + 12.75, 46.5), rot=90, bottom=False)
    place(fps["C1"], center=(-22.9, 41.4), rot=90, bottom=True)     # 3V3 腳（pin2）正下方背面
    place(fps["C2"], center=(-22.9, 45.0), rot=90, bottom=True)
    place(fps["R1"], center=(-19.0, 42.6), rot=0, bottom=True)      # EN（pin3）RC
    place(fps["C5"], center=(-19.0, 44.3), rot=0, bottom=True)
    place(fps["R3"], center=(-12.8, 41.2), rot=90, bottom=True)     # IO8（pin10）上拉
    place(fps["R2"], center=(-9.0, 51.5), rot=90, bottom=True)      # IO9（pin15）上拉
    # SYN480R：原 RF1 位置；轉 180° 讓 ANT（pin2）朝右，天線焊點遠離 ESP32 天線（左緣）
    place(fps["U3"], center=(-14.5, 63.0), rot=180, bottom=False)
    place(fps["Y1"], center=(-21.0, 59.6), rot=0, bottom=False)     # RO（pin8，左下）
    place(fps["R14"], center=(-19.6, 64.6), rot=90, bottom=False)   # SQ（pin7）→ GND
    place(fps["C17"], center=(-11.0, 67.0), rot=0, bottom=False)    # VDD（pin3）
    place(fps["L4"], center=(-10.0, 60.2), rot=90, bottom=False)    # ANT 腳並聯 47nH
    place(fps["C15"], center=(-8.6, 62.6), rot=180, bottom=False)   # 串聯 6.8pF
    place(fps["L3"], center=(-6.9, 60.2), rot=90, bottom=False)     # 天線端並聯 27nH
    place(fps["C16"], center=(-6.9, 65.0), rot=90, bottom=False)    # 天線端並聯 1.8pF
    place(fps["ANT1"], center=(-5.0, 62.6), rot=0, bottom=False)
    # 燒錄座 2×3：左上角（1×6 直排會壓到 H3 的 courtyard）
    place(fps["P2"], center=(-30.0, 79.6), rot=0, bottom=False)
    fps["ANT1"].SetExcludedFromBOM(True); fps["ANT1"].SetExcludedFromPosFiles(True)   # 天線線材手焊
    fps["P2"].SetExcludedFromPosFiles(True)                                             # 排針手焊（與 V2.0 相同）

    pcbnew.SaveBoard(OUT, board)


def do_nets():
    """第 2 步（新行程）：套網表、清低壓走線。同一行程 Remove/Add 過封裝後，SWIG 會回傳無型別物件"""
    board = pcbnew.LoadBoard(OUT)
    SN = sch_netlist()

    def NF(name):
        ni = board.FindNet(name)
        if ni is None:
            ni = pcbnew.NETINFO_ITEM(board, name); board.Add(ni)
        return ni
    # 4) 清走線：市電（已鎖定）保留，其餘（低壓走線、過孔、縫合過孔）全部重拉；鋪銅之後重建
    zones = [z for z in board.Zones() if not z.GetIsRuleArea()]      # 先全部取出再刪（刪到一半 SWIG 物件會失去型別）
    tracks = [t for t in board.GetTracks() if not MAINS(t.GetNetname())]
    for item in zones + tracks:
        board.Remove(item)
    # 5) 套用 V3.0 網表（沒有網路的焊盤，如 ESP32 EPAD 以外的重複腳，設成 net 0）
    no_net = board.FindNet(0)
    fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
    for ref, fp in fps.items():
        for pad in fp.Pads():
            full = SN.get((ref, pad.GetNumber()))
            pad.SetNet(NF(full) if full else no_net)
    pcbnew.SaveBoard(OUT, board)

    # 檢查：關鍵焊盤座標（Gerber）
    for ref, pn in (("U1", "1"), ("U1", "14"), ("U1", "15"), ("U1", "28"), ("U3", "2"), ("U3", "8"), ("P2", "1")):
        x, y = pad_g(fps[ref], pn)
        print(f"  {ref}.{pn}: ({x:.2f}, {y:.2f})")
    print(f"V3.0 -> {OUT}（{len(fps)} 顆零件）")


if __name__ == "__main__":
    if sys.argv[1:] == ["nets"]:
        do_nets()
    else:
        main()
        r = subprocess.run([sys.executable, __file__, "nets"])
        sys.exit(r.returncode)
