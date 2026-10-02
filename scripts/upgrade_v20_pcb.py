"""V1.3 PCB -> V2.0 PCB（第 1 段：換網表、增刪/搬移零件、畫市電走線、清掉低壓走線）。
之後由 route_v20.sh 交給 Freerouting 繞低壓網路，再鋪銅、跑 DRC。

座標一律用「原板 Gerber 座標」(mm，原點=板底邊中點，Y 向上)，與 import_v13_pcb.py 相同。
"""
import os, sys, re, math, subprocess, tempfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design

ROOT = os.path.abspath(os.path.join(HERE, ".."))
PRJ = os.path.join(ROOT, "hardware", "WO30109_FreshAir")
BASE = None
OUT = os.environ.get("OUT_PCB", os.path.join(PRJ, "WO30109_FreshAir.kicad_pcb"))
OX, OY, BW, BH = 100.0, 100.0, 63.4, 83.08
MM = pcbnew.FromMM
_IO = pcbnew.PCB_IO_KICAD_SEXPR()


def K(gx, gy): return OX + gx + BW / 2, OY + BH - gy
def G(kx, ky): return kx - OX - BW / 2, OY + BH - ky
def V(x, y): return pcbnew.VECTOR2I(MM(x), MM(y))


def sch_netlist():
    sch = os.path.join(PRJ, "WO30109_FreshAir.kicad_sch")
    tmp = tempfile.mktemp(suffix=".net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", tmp, sch], check=True, capture_output=True)
    txt = open(tmp).read(); out = {}
    for m in re.finditer(r'\(net\s*\(code "\d+"\)\s*\(name "([^"]*)"\)(.*?)\n\t\t\)', txt, re.S):
        for r, pn in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', m.group(2)):
            out[(r, pn)] = m.group(1)
    return out


def load_fp(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(ROOT, "hardware", "lib", "WOOW.pretty") if lib == "WOOW" else f"/usr/share/kicad/footprints/{lib}.pretty"
    fp = _IO.FootprintLoad(path, name); fp.SetFPID(pcbnew.LIB_ID(lib, name)); return fp


def set_lcsc(fp, val):
    """LCSC 欄位：已存在就改值，不存在就新增（隱藏）"""
    try:
        if fp.HasField("LCSC"):
            fp.GetField("LCSC").SetText(val); return
    except Exception:
        pass
    f = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER, "LCSC")
    f.SetText(val); f.SetVisible(False); f.SetLayer(pcbnew.F_Fab); fp.Add(f)


def pad_g(fp, num):
    for p in fp.Pads():
        if p.GetNumber() == num:
            return G(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))


def place(fp, center=None, rot=None, bottom=None, pads=None):
    """center：零件原點(Gerber)；pads：{pad號: (gx,gy)} 用兩個焊盤決定旋轉與位置"""
    if bottom is not None and fp.IsFlipped() != bottom:
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    if pads:
        best = None
        for r in (0, 90, 180, 270):
            fp.SetOrientationDegrees(r); fp.SetPosition(V(*K(0, 0)))
            (n1, t1), *rest = list(pads.items())
            p1 = pad_g(fp, n1); dx, dy = t1[0] - p1[0], t1[1] - p1[1]
            err = sum(math.hypot(pad_g(fp, n)[0] + dx - t[0], pad_g(fp, n)[1] + dy - t[1]) for n, t in rest)
            if best is None or err < best[0]: best = (err, r, dx, dy)
        _, r, dx, dy = best
        fp.SetOrientationDegrees(r); fp.SetPosition(V(*K(dx, dy)))
    else:
        if rot is not None: fp.SetOrientationDegrees(rot)
        fp.SetPosition(V(*K(*center)))


def apply(board, fps, P, SN, NF, load_fp_fn):
    """在 import_v13_pcb.py 建好的板子（尚未鋪銅）上套用 V2.0 變更。fps: {ref: FOOTPRINT}"""
    # 1) 移除 V2.0 不存在的零件
    for ref, fp in list(fps.items()):
        if ref not in P:
            board.Remove(fp); del fps[ref]
    for ref, fp in fps.items():
        fp.SetValue(P[ref]["value"])

    # 3) 擺放（只動需要動的；其餘維持 V1.3 原位）
    kx = {}
    for k in range(1, 5):
        c = pad_g(fps[f"K{k}"], "1"); kx[k] = c[0] + 3.81           # 繼電器中心 X（pad1 = 線圈 +12V）
    coil_y = pad_g(fps["K1"], "1")[1]
    # 原位鎖定（V2.0 網路改了，焊盤對位可能翻轉，直接用原板焊盤座標固定）
    place(fps["P1"], pads={"1": (2.28, 74.93), "2": (-2.72, 74.93)}, bottom=False)
    place(fps["U2"], pads={"1": (28.12, 40.68), "2": (28.12, 55.88), "3": (0.12, 40.68)}, bottom=False)
    place(fps["U3"], pads={"1": (-23.02, 38.75), "8": (-9.02, 38.75), "9": (-9.02, 53.60)}, bottom=False)
    place(fps["P3"], pads={"1": (17.78, 8.0), "8": (-17.78, 8.0)}, bottom=False)
    place(fps["L1"], pads={"1": (-22.4, 79.63), "2": (-22.4, 78.13)}, bottom=False)
    place(fps["R4"], pads={"1": (-22.4, 74.98), "2": (-22.4, 76.48)}, bottom=False)
    # 市電入口：F1、RV1 往右，避開 P1
    place(fps["F1"], pads={"1": (7.8, 77.5), "2": (12.88, 77.5)}, bottom=False)
    place(fps["RV1"], pads={"1": (19.0, 77.5), "2": (17.7, 72.5)}, bottom=False)
    # RF1：與原板相同，模組固定孔對準板子固定孔（H3），轉 180°
    place(fps["RF1"], pads={"1": (-16.45, 66.56), "6": (-6.45, 66.56)}, bottom=False)
    place(fps["P2"], pads={"1": (-29.8, 78.62), "4": (-29.8, 71.0)}, bottom=False)
    place(fps["S1"], center=(-15.7, 79.3), rot=0, bottom=True)
    # 降壓電源（IRM 下方背面，離市電 >6.4mm）
    place(fps["U4"], center=(4.8, 51.0), rot=0, bottom=True)
    place(fps["L2"], center=(9.6, 51.0), rot=0, bottom=True)
    place(fps["C12"], center=(4.8, 48.0), rot=0, bottom=True)
    place(fps["C14"], center=(1.6, 53.8), rot=90, bottom=True)
    place(fps["C7"], center=(13.3, 49.3), rot=90, bottom=True)
    place(fps["C13"], center=(13.3, 52.8), rot=90, bottom=True)
    # STM32 周邊：繞 U1 一圈（U1 中心約 -12.78, 46.02）
    place(fps["C1"], center=(-12.8, 52.6), rot=0, bottom=True)
    place(fps["C2"], center=(-5.6, 45.6), rot=90, bottom=True)          # 讓出 U1 第 42–44 腳的出腳通道
    place(fps["C3"], center=(-6.6, 48.8), rot=90, bottom=True)
    place(fps["C4"], center=(-6.6, 42.4), rot=90, bottom=True)
    place(fps["C10"], center=(-19.4, 44.0), rot=90, bottom=True)
    place(fps["R1"], center=(-19.4, 47.4), rot=90, bottom=True)
    place(fps["C5"], center=(-21.0, 47.4), rot=90, bottom=True)
    place(fps["R2"], center=(-19.4, 50.8), rot=90, bottom=True)
    place(fps["R3"], center=(-21.0, 50.8), rot=90, bottom=True)
    place(fps["R13"], center=(-24.6, 36.3), rot=0, bottom=True)
    place(fps["TP1"], center=(-6.4, 54.2), rot=0, bottom=True)
    # 繼電器驅動：二極體夾在兩個線圈腳中間，MOS/電阻在上一排（離市電 >6.4mm、不壓 U1）
    for k in range(1, 5):
        xc = kx[k]
        place(fps[f"D{k}"], pads={"1": (xc - 1.65, coil_y), "2": (xc + 1.65, coil_y)}, bottom=True)
        place(fps[f"Q{k}"], center=(xc + 2.3, coil_y + 3.0), rot=0, bottom=True)
        place(fps[f"R{4 + k}"], center=(xc - 1.5, coil_y + 3.0), rot=0, bottom=True)
        if k == 3:                                               # K3 左邊是 IRM 的 GND 腳：下拉電阻放到 MOS 上一排
            place(fps["R11"], center=(xc + 2.3, coil_y + 5.5), rot=0, bottom=True)
        else:
            place(fps[f"R{8 + k}"], center=(xc - 3.9, coil_y + 3.2), rot=90, bottom=True)
    for h in ("H1", "H2", "H3", "H4"):                         # 固定孔不進 BOM／座標檔
        fps[h].SetExcludedFromBOM(True); fps[h].SetExcludedFromPosFiles(True)
    fps["TP1"].SetExcludedFromBOM(True)

    # 4) 套用 V2.0 網表到所有焊盤
    for ref, fp in fps.items():
        for pad in fp.Pads():
            full = SN.get((ref, pad.GetNumber()))
            if full: pad.SetNet(NF(full))

    # 5) 清走線：保留繼電器輸出區（DO_*）原走線，其餘全部重拉
    keep = lambda n: False                                   # 全部重拉（市電由 mains_router，低壓由 Freerouting）
    for t in list(board.GetTracks()):
        if not keep(t.GetNetname()): board.Remove(t)
    for z in list(board.Zones()):
        if not z.GetIsRuleArea(): board.Remove(z)

    # 6) 市電走線（固定位置；寬度：負載電流路徑 1.5mm、IRM 供電 1.0mm）
    def track(net, layer, pts, w):
        for a, b in zip(pts, pts[1:]):
            t = pcbnew.PCB_TRACK(board); t.SetStart(V(*K(*a))); t.SetEnd(V(*K(*b)))
            t.SetWidth(MM(w)); t.SetLayer(layer); t.SetNet(NF(net)); t.SetLocked(True); board.Add(t)
    TOP, BOT = pcbnew.F_Cu, pcbnew.B_Cu
    p1_L, p1_N = pad_g(fps["P1"], "1"), pad_g(fps["P1"], "2")
    irm_L, irm_N = pad_g(fps["U2"], "1"), pad_g(fps["U2"], "2")
    p3_1 = pad_g(fps["P3"], "1")
    f1a, f1b = pad_g(fps["F1"], "1"), pad_g(fps["F1"], "2")
    rva, rvb = pad_g(fps["RV1"], "1"), pad_g(fps["RV1"], "2")
    track("/AC_L_IN", TOP, [p1_L, (p1_L[0], 76.0), (f1a[0] - 1.5, f1a[1]), f1a], 1.5)
    # 右緣市電走道放在 x=30.2（離板邊 0.75mm），讓 H2/H4 固定孔孔邊離市電 ≥2.3mm
    track("/AC_L", TOP, [f1b, rva, (28.0, f1b[1]), (30.2, f1b[1] - 2.2), (30.2, 63.0),
                         (23.6, 57.4), (23.6, 50.6), (irm_L[0], 46.0), irm_L], 1.5)
    track("/AC_L", BOT, [irm_L, (30.2, irm_L[1] - 2.1), (30.2, 13.2), (26.4, 9.4), (19.3, 9.4), p3_1], 1.5)
    # AC_N 走道 x=22；經過 H2（24.95, 66.56）那段往內彎到 x=20
    track("/AC_N", BOT, [p1_N, (p1_N[0], 79.6), (p1_N[0] + 2.4, 82.0), (20.0, 82.0), (22.0, 80.0),
                         (22.0, 71.0), (20.0, 69.0), (20.0, 64.0), (22.0, 62.0),
                         (22.0, 58.0), (24.1, irm_N[1]), irm_N], 1.0)
    track("/AC_N", BOT, [rvb, (22.0, rvb[1])], 1.0)

    # 7) 繼電器輸出（DO_*）：雙層格點佈線，市電間 1.5mm、對低壓 6.4mm
    import mains_router
    is_mains = lambda n: n.startswith("/AC_") or n.startswith("/DO_") or n.startswith("unconnected-(K")
    R = mains_router.Router(board, is_mains, cl_mm=1.6, selv_cl_mm=6.5, width=1.5)
    for net in ["/DO_COM", "/DO_1_NO", "/DO_2_NO", "/DO_3_NO", "/DO_4_NO", "/DO_3_NC", "/DO_4_NC"]:
        segs = R.route_net(net)
        R.commit(net, segs, NF(net))
        print(f"  {net}: {sum(1 for s_ in segs if s_[0]=='T')} 段, {sum(1 for s_ in segs if s_[0]=='V')} 過孔")
    return dict(relay_x={k: round(v, 2) for k, v in kx.items()}, coil_y=round(coil_y, 2))


if __name__ == "__main__":
    raise SystemExit("請用 DESIGN_VERSION=2.0 python3 import_v13_pcb.py")
