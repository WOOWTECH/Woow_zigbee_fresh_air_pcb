"""V3.1 PCB -> V3.2／V3.3 PCB。既有走線、市電、鋪銅規則都不動，只用格點 A*（mains_router，對市電 6.5mm）
佈新增的網路，不跑 Freerouting。
  V3.2：J5 那一段上緣加長 10mm 放 4 路光耦 DI，拿掉指撥 S1。
  V3.3：再加 NFC（U6 ST25DV04KC＋板上線圈 L5）；J5 改到左側板邊，左上角給線圈，左半段上緣到 y＝107.2。

  python scripts/gen_schematic.py 3.3                      # 先產生原理圖（kicad-sch-api）
  WO_VER=3.3 python3 scripts/upgrade_v32_pcb.py           # 從 BASE_REF 的 V3.1 PCB 開始，結果寫回專案 PCB（預設 3.2）

座標用「原板 Gerber 座標」（mm，原點＝板底邊中點，Y 向上），與 upgrade_v30_pcb.py 相同。
每一步各開一個 Python 行程（KiCad 10 的 SWIG 在同一行程 Remove/Add 封裝後會回傳無型別物件）。
"""
import os, subprocess, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design
import mains_router
from upgrade_v20_pcb import K, V, place, pad_g, set_lcsc, sch_netlist, ROOT, PRJ
from upgrade_v30_pcb import load_fp

OUT = os.environ.get("OUT_PCB", os.path.join(PRJ, "WO30109_FreshAir.kicad_pcb"))
BASE_REF = os.environ.get("BASE_REF", "f64c97b")     # V3.1 PCB（commit「hw: V3.1 433 匹配重新選值…」）
REL = "hardware/WO30109_FreshAir/WO30109_FreshAir.kicad_pcb"
VER = os.environ.get("WO_VER", "3.2")
V33 = VER >= "3.3"
NOTCH_GX = -5.8                                        # L 形轉角（Gerber x）：J5 courtyard 右緣 −5.9、P1 courtyard 左緣 −5.7
OLD_TOP = 83.08                                        # Gerber y
# 新上緣（只加在 NOTCH_GX 左邊那一段）：V3.2 加 10mm；V3.3 的 J5 直立貼左側板邊，courtyard 25.6mm 要在 P2（y≤81.4）上方
NEW_TOP = 107.2 if V33 else OLD_TOP + 10.0
NEW = ["J5", "U5", "R23"] + [f"R{14 + k}" for k in range(1, 5)] + [f"C{17 + k}" for k in range(1, 5)]
if V33:
    NEW += ["U6", "L5", "C22", "C24", "C23", "R24", "R25"]
J5_PIN_GX = -24.2                                      # V3.3：J5 焊盤直排的 Gerber x（courtyard 左緣貼板邊 −31.7，深 7.5）
J5_PIN1_GY = NEW_TOP - 23.3                            # V3.3：開口朝左時 Pin1（+12V）在最下，往上每 3.5mm 一腳
# 被新零件佔到、要拆掉重佈的既有銅：S1 專用的 GND 支線、P2 的 +3V3 供電（沿 y≈102 穿過 U5 位置）、擋到的縫合過孔
RIP_NETS = ("/Mode_bit0", "/Mode_bit1")


def kx(gx): return K(gx, 0)[0]
def ky(gy): return K(0, gy)[1]


def step_place():
    P = design.build(VER)
    lib_fp = {ref: load_fp(P[ref]["footprint"]) for ref in NEW}     # 封裝要在 LoadBoard 前載好
    base = subprocess.run(["git", "-C", ROOT, "show", f"{BASE_REF}:{REL}"], check=True, capture_output=True).stdout
    open(OUT, "wb").write(base)
    b = pcbnew.LoadBoard(OUT)
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}
    b.Remove(fps.pop("S1"))
    for ref in NEW:
        fp = lib_fp[ref]; fp.SetReference(ref); fp.SetValue(P[ref]["value"]); set_lcsc(fp, P[ref]["lcsc"] or "")
        b.Add(fp); fps[ref] = fp
    # 板框：只有 J5 那一段（左邊到 NOTCH_GX）往上 10mm，形成 L 形。P1（AC IN）是開口朝板邊的插拔座，
    # 插頭與 230V 電線會伸出原板緣；那裡若有板子，插頭就壓在低壓 GND 鋪銅上方（空氣間隙遠小於 6.4mm）
    for d in list(b.GetDrawings()):
        if d.GetLayerName() == "Edge.Cuts" and abs(pcbnew.ToMM(d.GetStart().y) - ky(OLD_TOP)) < 0.01 \
                and abs(pcbnew.ToMM(d.GetEnd().y) - ky(OLD_TOP)) < 0.01:
            b.Remove(d)
    pts = [(-31.7, OLD_TOP), (-31.7, NEW_TOP), (NOTCH_GX, NEW_TOP), (NOTCH_GX, OLD_TOP), (31.7, OLD_TOP)]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        seg = pcbnew.PCB_SHAPE(b); seg.SetShape(pcbnew.SHAPE_T_SEGMENT); seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetStart(V(kx(x0), ky(y0))); seg.SetEnd(V(kx(x1), ky(y1))); seg.SetWidth(pcbnew.FromMM(0.05)); b.Add(seg)
    if V33:
        # J5 直立貼左側板邊，開口朝左（這個封裝開口朝左時 Pin1 一定在下）：由下往上 +12V、IN1–IN4、COM、GND
        place(fps["J5"], pads={"1": (J5_PIN_GX, J5_PIN1_GY), "7": (J5_PIN_GX, J5_PIN1_GY + 21.0)}, bottom=False)
        # R15–R18：背面、J5 本體底下（板邊與焊盤之間），兩欄交錯，各自對齊所接的 INk 腳
        for k in range(1, 5):
            gx = -29.7 if k % 2 else -26.9
            place(fps[f"R{14 + k}"], center=(gx, J5_PIN1_GY + 3.5 * k), rot=90, bottom=True)
        # R23（1.5k 限流）：背面、J5 本體底下最上方（左欄空位，R17 以上）；+12V_DI 從這裡往下接 Pin1。
        # 放上面而不是 Pin1 旁邊：下方那條 y≈100 的窄帶要留給 DI_A／COM 出線到 U5
        place(fps["R23"], center=(-29.7, J5_PIN1_GY + 19.0), rot=90, bottom=True)
    else:
        # J5：開口朝板邊，courtyard 上緣貼新板邊；Pin1（+12V）在左
        place(fps["J5"], center=(-18.7, NEW_TOP - 7.5), rot=0, bottom=False)
        # R15–R18：背面、J5 本體下方，直立在 IN1–IN4 腳正上方（腳在 y＝NEW_TOP−7.5）
        for k in range(1, 5):
            place(fps[f"R{14 + k}"], center=(-18.7 - 10.5 + 3.5 * k, NEW_TOP - 3.5), rot=90, bottom=True)   # J5 Pin k+1
        # R23（1.5k 限流）：背面、J5 Pin1 正上方，與 R15–R18 同排；Pin2（+12V_DI）朝下接 J5，Pin1（+12V）朝板邊
        place(fps["R23"], center=(-18.7 - 10.5, NEW_TOP - 3.5), rot=270, bottom=True)
    fps["J5"].SetExcludedFromPosFiles(True)                          # 插拔座手焊（同 P1、P3）
    # U5：背面、原 S1 位置；LED 腳（1–8）朝 J5，集極（9–16）朝 U1
    place(fps["U5"], center=(-17.2, OLD_TOP - 4.9), rot=270, bottom=True)   # 背面轉 270°：Pin1 在左上，與 J5 IN1 同側
    # 濾波 10nF：U5 每個集極腳（16/14/12/10，間距 2.54）正下方，Pin1（DI）朝 U5、Pin2（GND）朝下接鋪銅；上拉用 ESP32 內建
    for k in range(1, 5):
        cx = mains_router.to_g(fps["U5"].FindPadByNumber(str(18 - 2 * k)).GetPosition())[0]
        place(fps[f"C{17 + k}"], center=(cx, OLD_TOP - 11.6), rot=270, bottom=True)
    if V33:
        # NFC 線圈 L5：左上角，J5 本體右邊到 L 形轉角（x 110.5–125.4、y 76.4–91.0 KiCad），上下兩層禁布
        place(fps["L5"], center=(-13.75, NEW_TOP - 0.52 - 7.3), rot=0, bottom=False)
        fps["L5"].SetExcludedFromBOM(True); fps["L5"].SetExcludedFromPosFiles(True)     # 純銅箔
        # U6 ST25DV04KC：正面、線圈正下方（AC0/AC1 在左側靠線圈焊盤），C22 調諧、C24 微調空位夾在中間
        place(fps["U6"], center=(-10.3, NEW_TOP - 20.4), rot=0, bottom=False)
        place(fps["C22"], center=(-15.8, NEW_TOP - 18.7), rot=0, bottom=False)
        place(fps["C24"], center=(-15.8, NEW_TOP - 21.0), rot=0, bottom=False)
        fps["C24"].SetExcludedFromBOM(True); fps["C24"].SetExcludedFromPosFiles(True)   # 不上件，打樣後依實測微調
        place(fps["C23"], center=(-7.7, NEW_TOP - 19.8), rot=90, bottom=True)          # VCC（Pin8）去耦，U6 正背面
        place(fps["R24"], center=(-10.7, NEW_TOP - 19.2), rot=0, bottom=True)          # SDA／SCL 上拉，U6 正背面
        place(fps["R25"], center=(-10.7, NEW_TOP - 21.6), rot=0, bottom=True)
    pcbnew.SaveBoard(OUT, b)


def _coil_bridge(b):
    """NFC 線圈最內圈端點（內側「2」）→ 線圈外的「2」：背面直線。封裝圖形不算連接，所以畫成真正的走線"""
    f = b.FindFootprintByReference("L5")
    p2 = sorted([p for p in f.Pads() if p.GetNumber() == "2"], key=lambda p: p.GetPosition().y)
    t = pcbnew.PCB_TRACK(b); t.SetStart(p2[0].GetPosition()); t.SetEnd(p2[1].GetPosition())
    t.SetWidth(pcbnew.FromMM(0.2)); t.SetLayer(pcbnew.B_Cu); t.SetNet(p2[0].GetNet()); t.SetLocked(True); b.Add(t)


def step_nets():
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
    # 拆銅：舊指撥訊號；S1 專用 GND 支線（y≈101.6 那條與到 S1 腳的兩段）；P2 的 +3V3 供電（y≈102.2）；U5／新零件範圍內的縫合過孔
    U5_BOX = (kx(-23.0), kx(-6.0), ky(OLD_TOP - 0.2), ky(OLD_TOP - 13.2))    # x0,x1,y_top,y_bot（KiCad）
    def inside(p, box):
        x, y = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
        return box[0] <= x <= box[1] and box[2] <= y <= box[3]
    kill = []
    for t in b.GetTracks():
        n = t.GetNetname()
        if n in RIP_NETS:
            kill.append(t); continue
        if t.GetClass() == "PCB_VIA" and n in ("/GND", "/+3V3") and inside(t.GetPosition(), U5_BOX):
            kill.append(t); continue
        if t.GetClass() == "PCB_TRACK" and t.GetLayer() == pcbnew.B_Cu and n in ("/GND", "/+3V3"):
            if inside(t.GetStart(), U5_BOX) or inside(t.GetEnd(), U5_BOX):
                kill.append(t)
    # 拆掉的 +3V3 過孔原本把正面主幹接到 P2；接在這顆過孔上的正面走線也一起拆（否則留下 12.8mm 的懸空殘段）
    gone = [t.GetPosition() for t in kill if t.GetClass() == "PCB_VIA" and t.GetNetname() == "/+3V3"]
    for t in b.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetNetname() == "/+3V3" and t not in kill:
            if any((t.GetStart() - g).EuclideanNorm() < 50000 or (t.GetEnd() - g).EuclideanNorm() < 50000 for g in gone):
                kill.append(t)
    for t in kill:
        b.Remove(t)
    # 剩下的 +3V3 殘段：反覆刪「有一端什麼都沒接」的走線（只限原 P2 供電路徑附近，主幹其他地方不碰）
    STUB_BOXES = [(kx(-15.0), kx(-12.0), ky(OLD_TOP), ky(OLD_TOP - 18.0)),     # 原 P2 供電路徑
                  (kx(-31.7), kx(-22.0), ky(OLD_TOP + 5.0), ky(OLD_TOP - 12.0))]  # P2 Pin1 附近（V3.3 擺件後的殘段）
    def ends(item):
        return [item.GetPosition()] if item.GetClass() == "PCB_VIA" else [item.GetStart(), item.GetEnd()]
    for _ in range(20):
        net_items = [t for t in b.GetTracks() if t.GetNetname() == "/+3V3"]
        pads = [p.GetPosition() for fp in b.GetFootprints() for p in fp.Pads() if p.GetNetname() == "/+3V3"]
        dangling = []
        for t in net_items:
            if t.GetClass() != "PCB_TRACK" or not any(inside(t.GetStart(), bx) or inside(t.GetEnd(), bx) for bx in STUB_BOXES):
                continue
            for e in ends(t):
                touch = any((e - q).EuclideanNorm() < 50000 for o in net_items if o is not t for q in ends(o))
                touch = touch or any((e - q).EuclideanNorm() < 50000 for q in pads)
                if not touch:
                    dangling.append(t); break
        if not dangling: break
        for t in dangling: b.Remove(t)
        kill += dangling
    # 鋪銅外框跟著板框上移
    for z in b.Zones():
        if z.GetIsRuleArea(): continue
        ol = z.Outline()
        for i in range(ol.TotalVertices()):
            v = ol.CVertex(i)
            if abs(pcbnew.ToMM(v.y) - ky(OLD_TOP - 0.5)) < 0.05:
                ol.SetVertex(i, V(pcbnew.ToMM(v.x), ky(NEW_TOP - 0.5)))
    if V33:
        _coil_bridge(b)
    pcbnew.SaveBoard(OUT, b)
    print(f"  拆除 {len(kill)} 段既有銅（指撥訊號、S1 GND 支線、P2 +3V3 供電、U5 範圍內縫合過孔）")


PRE = OUT.replace(".kicad_pcb", ".pre_long.kicad_pcb")
FAILED = OUT.replace(".kicad_pcb", ".failed.kicad_pcb")


def _with_project(path):
    """暫存板檔旁邊放一份正式的 .kicad_pro：pcbnew 載入沒有專案檔的板子會用預設網路類別與間距，
    之後 SaveBoard 到正式路徑時會把預設值寫回正式 .kicad_pro（市電 1.5mm、POWER 類別全被蓋掉）"""
    import shutil
    shutil.copy(OUT.replace(".kicad_pcb", ".kicad_pro"), path.replace(".kicad_pcb", ".kicad_pro"))
    return path


def _cleanup_temp():
    for f in (PRE, FAILED):
        for g in (f, f.replace(".kicad_pcb", ".kicad_pro"), f.replace(".kicad_pcb", ".kicad_prl")):
            if os.path.exists(g): os.remove(g)
U1_PIN = {1: "26", 2: "23", 3: "20", 4: "19"} if V33 else {1: "20", 2: "19", 3: "18", 4: "17"}


def _router(b):
    mains_router.set_board(mains_router.board_height(b))
    MAINS = lambda n: n.startswith("/AC_") or n.startswith("/DO_") or n.startswith("unconnected-(K")
    # 間距 0.3（規則 0.127；格點 0.1mm 量化與斜線點陣化會吃掉一些，0.25 時仍有過孔貼線）；
    # stamp_r=0：細腳焊盤要真的接到焊盤中心；keep_cl：清自己焊盤時別條網路的銅照樣擋
    R = mains_router.Router(b, lambda n: not MAINS(n), cl_mm=0.3, selv_cl_mm=6.5, width=0.2, edge=0.5, npth_cl=0.5)
    # 433 匹配網路（L3、L4、C15、C16、ANT1）上下兩層都不讓新走線經過：雙層板上射頻匹配底下要完整 GND 回流，
    # 新線切過去會讓 V3.1 才調好的匹配偏移。用一排粗線把這塊塗成障礙
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}
    xs, ys = [], []
    for ref in ("L3", "L4", "C15", "C16", "ANT1"):
        bb = fps[ref].GetBoundingBox(False)
        for v in (bb.GetOrigin(), bb.GetEnd()):
            gx, gy = mains_router.to_g(v); xs.append(gx); ys.append(gy)
    x0, x1, y0, y1 = min(xs) - 0.5, max(xs) + 0.5, min(ys) - 0.5, max(ys) + 0.5
    y = y0
    while y <= y1:
        for L in ("F", "B"):
            R.extra.append(("__RF_KEEPOUT__", L, (x0, y), (x1, y), 0.6))
        y += 0.4
    return R


def _in_own_keepout(fp, pad):
    """線圈 L5 的「2」有兩個焊盤：最內圈那個在自己的禁布區裡，只能經背面跨線接，不能當佈線端點"""
    return any(z.GetIsRuleArea() and z.Outline().Contains(pad.GetPosition()) for z in fp.Zones())


def _route(b, R, jobs, fail_path=None):
    fps = {fp.GetReference(): fp for fp in b.GetFootprints()}

    def only(refs_pins):
        """Router.terminals 只取指定焊盤（依給的順序）：既有網路只接新零件，不重佈整條網路"""
        def terms(net):
            out = []
            for ref, pn in refs_pins:
                for pad in fps[ref].Pads():
                    if pad.GetNumber() == pn and pad.GetNetname() == net and not _in_own_keepout(fps[ref], pad):
                        L = ["F", "B"] if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else (["B"] if pad.IsOnLayer(pcbnew.B_Cu) else ["F"])
                        out.append((mains_router.to_g(pad.GetPosition()), L))
            return out
        return terms

    for net, pins in jobs:
        orig = R.terminals
        if pins: R.terminals = only(pins)
        try:
            segs = R.route_net(net, via_cost=15, clear_mm=None, stamp_r=0.0, keep_cl=0.15)
        except RuntimeError as e:
            if fail_path: pcbnew.SaveBoard(fail_path, b)      # 留下佈到一半的結果，方便看是誰擋路
            print(f"  {e}"); return False
        finally:
            R.terminals = orig
        R.commit(net, segs, b.FindNet(net))
        for t in b.GetTracks():
            if t.GetNetname() == net and t.GetClass() == "PCB_VIA":
                t.SetDrill(pcbnew.FromMM(0.3)); t.SetWidth(pcbnew.FromMM(0.6))
        print(f"  {net:10s} {sum(1 for x in segs if x[0] == 'T'):3d} 段 {sum(1 for x in segs if x[0] == 'V')} 過孔")
    return True


def step_route_pre():
    """U5 周邊：DI_IN（J5→電阻）、DI_A（電阻→LED 腳，從 J5 腳縫往下）、DI_COM（LED 腳 A/COM 交錯，只能從 U5 本體
    下方兩排焊盤中間橫向接）、4 條「集極 → 正下方 10nF」2.6mm 短線（先佔好，長線才不會切過）"""
    b = pcbnew.LoadBoard(OUT); R = _router(b)
    jobs = [(f"/DI_IN{k}", None) for k in range(1, 5)] + [(f"/DI_A{k}", None) for k in range(1, 5)] + [("/DI_COM", None)]
    jobs += [(f"/DI_{k}", [("U5", str(18 - 2 * k)), (f"C{17 + k}", "1")]) for k in range(1, 5)]
    if V33:   # 線圈 → 調諧電容 → U6 的短線，以及 U6 的去耦、上拉
        jobs += [("/NFC_AC0", [("L5", "1"), ("C22", "1"), ("C24", "1"), ("U6", "2")]),
                 ("/NFC_AC1", [("L5", "2"), ("C22", "2"), ("C24", "2"), ("U6", "3")]),
                 ("/NFC_SDA", [("U6", "5"), ("R24", "2")]), ("/NFC_SCL", [("U6", "6"), ("R25", "2")]),
                 # 順序（試出來的）：I2C 長線 → +12V → DI 長線（route_long 試排列）→ +3V3。
                 # I2C 接 U1 上排最右邊的 18、17 腳（DI 長線在它左邊，彼此不交叉）；SCL 在 SDA 右邊，先佈外側
                 ("/NFC_SCL", [("R25", "2"), ("U1", "17")]), ("/NFC_SDA", [("R24", "2"), ("U1", "18")]),
                 # +12V：R23 在左上角，從 U2 拉過來
                 ("/+12V", [("R23", "1"), ("U2", "4")]), ("/+12V_DI", None)]
    if not _route(b, R, jobs, _with_project(FAILED)): sys.exit(1)
    pcbnew.SaveBoard(_with_project(PRE), b)


def step_route_long(order):
    """「10nF → U1 上排」4 條長線（依 order），再佈電源：P2 的 +3V3（接 U3 VDD；最近的 C17 貼著射頻禁佈區）、
    +12V（U2→R23）、+12V_DI。
    長線的順序決定中間兩隻腳（U1 第 19、18 腳）會不會被左右兩條的過孔包住，所以由 main 依序試所有排列"""
    b = pcbnew.LoadBoard(PRE); R = _router(b)
    jobs = [(f"/DI_{k}", [(f"C{17 + k}", "1"), ("U1", U1_PIN[k])]) for k in map(int, order)]
    if V33:   # +3V3 一路接 U6、C23、上拉，再接回 P2 與 U3（I2C 長線已在 route_pre 佈好）
        jobs += [("/+3V3", [("C23", "1"), ("U6", "8"), ("R24", "1"), ("R25", "1"), ("P2", "1"), ("U3", "3")])]
    else:
        jobs += [("/+3V3", [("P2", "1"), ("U3", "3")]), ("/+12V", [("R23", "1"), ("U2", "4")]), ("/+12V_DI", None)]
    if not _route(b, R, jobs, _with_project(FAILED)): sys.exit(2)
    pcbnew.SaveBoard(OUT, b)


def step_route():
    import itertools
    r = subprocess.run([sys.executable, __file__, "route_pre"])
    if r.returncode: sys.exit(r.returncode)
    for perm in itertools.permutations("1234"):
        order = "".join(perm)
        print(f"  長線順序 DI_{' DI_'.join(order)}")
        if subprocess.run([sys.executable, __file__, "route_long", order]).returncode == 0:
            _cleanup_temp(); return
    sys.exit("所有長線順序都佈不通")


def step_silk():
    """J5 腳位標示（背面絲印，端子本體擋住正面）＋版次"""
    b = pcbnew.LoadBoard(OUT)
    for i, name in enumerate(["12V", "IN1", "IN2", "IN3", "IN4", "COM", "GND"]):
        t = pcbnew.PCB_TEXT(b); t.SetText(name); t.SetLayer(pcbnew.B_SilkS)
        if V33:   # J5 直立：標在焊盤右邊（背面）
            t.SetPosition(V(kx(J5_PIN_GX + 3.1), ky(J5_PIN1_GY + 3.5 * i)))
        else:
            t.SetPosition(V(kx(-18.7 - 10.5 + 3.5 * i), ky(NEW_TOP - 1.2)))
        t.SetTextSize(V(0.8, 0.8)); t.SetTextThickness(pcbnew.FromMM(0.12)); t.SetMirrored(True)
        b.Add(t)
    tb = b.GetTitleBlock(); tb.SetRevision(VER); b.SetTitleBlock(tb)
    pcbnew.SaveBoard(OUT, b)


def step_stitch():
    """新區域（上緣帶狀區＋U5＋DI 走線經過處）補 GND 縫合過孔：新走線把背面鋪銅切出孤島，要接回正面 GND"""
    env = dict(os.environ, STITCH_REGION=f"{kx(-31.7)},{ky(NEW_TOP)},{kx(-3.0)},{ky(51.0)}")
    st = os.path.join(HERE, "add_stitching.py"); fill = [sys.executable, os.path.join(HERE, "route_v20.py"), "fill"]
    subprocess.run(fill, check=True, capture_output=True)
    for args in (["3.0"], ["--near-signal"]):
        r = subprocess.run([sys.executable, st] + args, env=env, check=True, capture_output=True, text=True)
        print("  " + r.stdout.strip().splitlines()[-1])
        subprocess.run(fill, check=True, capture_output=True)
    r = subprocess.run([sys.executable, st, "--prune"], env=env, check=True, capture_output=True, text=True)
    print("  " + r.stdout.strip().splitlines()[-1])
    subprocess.run([sys.executable, __file__, "dedupe_vias"], check=True)
    subprocess.run(fill, check=True, capture_output=True)
    r = subprocess.run([sys.executable, st, "--islands"], check=True, capture_output=True, text=True)
    print("\n".join("  " + l.strip() for l in r.stdout.strip().splitlines() if "孤島" in l))


def step_dedupe_vias():
    """訊號過孔旁補 GND 過孔時，會在 V3.0 建置已補過的同一點再放一顆（DRC holes_co_located）"""
    b = pcbnew.LoadBoard(OUT); seen = set(); dup = []
    for t in b.GetTracks():
        if t.GetClass() != "PCB_VIA": continue
        key = (t.GetNetname(), round(pcbnew.ToMM(t.GetPosition().x), 3), round(pcbnew.ToMM(t.GetPosition().y), 3))
        if key in seen: dup.append(t)
        else: seen.add(key)
    for t in dup: b.Remove(t)
    pcbnew.SaveBoard(OUT, b); print(f"  移除重複過孔 {len(dup)} 顆")


STEPS = {"place": step_place, "nets": step_nets, "route": step_route, "silk": step_silk, "stitch": step_stitch,
         "route_pre": step_route_pre, "dedupe_vias": step_dedupe_vias}

if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "route_long":
        step_route_long(sys.argv[2])
    elif len(sys.argv) > 1:
        STEPS[sys.argv[1]]()
    else:
        pro = OUT.replace(".kicad_pcb", ".kicad_pro"); pro_bytes = open(pro, "rb").read()   # SaveBoard 會把預設欄位補進 .kicad_pro
        for s in ("place", "nets", "route", "silk", "stitch"):
            print(f"== {s}")
            r = subprocess.run([sys.executable, __file__, s])
            if r.returncode: sys.exit(r.returncode)
        subprocess.run([sys.executable, os.path.join(HERE, "route_v20.py"), "fill"], check=True)
        open(pro, "wb").write(pro_bytes)                                                    # 值沒變，只是避免無謂的 diff
        print(f"V{VER} -> {OUT}")
