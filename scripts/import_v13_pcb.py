"""把原設計 V1.3 的 Gerber 銅箔轉成 KiCad PCB（需本機原始 Gerber，不隨 repo 公開）。

  ORIG_GERBER=/path/to/20250709_WO_30109_Zigbee_Gerber_V1.3  python3 import_v13_pcb.py

做法
  1. 板框、走線（D01 線段）、過孔（0.3mm 鑽孔）照 Gerber 原樣搬進來；鋪銅另以 KiCad zone 重建。
  2. 每顆零件用 design.py 指定的 KiCad 封裝，以「焊盤對齊」求出位置／旋轉／正反面。
  3. 網路名稱：Gerber 銅箔連通（含鋪銅，0.05mm 點陣）→ 對到封裝焊盤 → 原理圖網路。
     同一條 Gerber 銅網路若對到兩個不同原理圖網路，代表重繪的原理圖有錯，會列出來。
"""
import os, sys, re, csv, io, math, json, pickle, itertools
import numpy as np
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import design

GB = os.environ.get("ORIG_GERBER", os.path.expanduser(
    "~/eda-tools/wo30109/odoo/gerber/20250709_WO_30109_Zigbee_Gerber_V1.3"))
ANA = os.environ.get("ORIG_ANALYSIS", os.path.expanduser("~/eda-tools/wo30109/analysis"))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PRJ = os.path.join(ROOT, "hardware", "WO30109_FreshAir")
OUT = os.environ.get("OUT_PCB", os.path.join(PRJ, "WO30109_FreshAir.kicad_pcb"))
VERSION = os.environ.get("DESIGN_VERSION", "1.3")
OX, OY, BW, BH = 100.0, 100.0, 63.4, 83.08           # KiCad 板框左上角 & 尺寸
MM = pcbnew.FromMM


def K(gx, gy):                                         # Gerber(mm, 原點底中) -> KiCad(mm)
    return OX + gx + BW / 2, OY + BH - gy


def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


# ---------- Gerber 讀取 ----------
def parse(path):
    txt = open(path, errors="ignore").read()
    ap = {m.group(1): (m.group(2), [float(v) for v in m.group(3).split("X")])
          for m in re.finditer(r"%ADD(\d+)([CRO]),([0-9.X]+)\*%", txt)}
    body = re.sub(r"%[^%]*%", "", txt)
    cur = None; x = y = 0.0; reg = False; draws = []; flashes = []
    for t in body.split("*"):
        t = t.strip()
        if t == "G36": reg = True; continue
        if t == "G37": reg = False; continue
        m = re.fullmatch(r"(?:G0?1)?D(\d{2,})", t)
        if m and m.group(1) in ap: cur = m.group(1); continue
        m = re.match(r"^(?:G0?1)?(?:X(-?\d+))?(?:Y(-?\d+))?D0?([123])$", t)
        if not m: continue
        nx = int(m.group(1)) * 1e-4 if m.group(1) else x
        ny = int(m.group(2)) * 1e-4 if m.group(2) else y
        if not reg and cur:
            if m.group(3) == "1" and (nx, ny) != (x, y):
                draws.append((x, y, nx, ny, ap[cur][1][0]))
            elif m.group(3) == "3":
                flashes.append((nx, ny, ap[cur][0], ap[cur][1]))
        x, y = nx, ny
    return draws, flashes


def drills():
    txt = open(os.path.join(GB, "WO_30109_Zigbee.TXT")).read()
    tools = {str(int(a)): float(c) for a, c in re.findall(r"^T(\d+)F\d+S\d+C([\d.]+)", txt, re.M)}
    m = re.search(r"FILE_FORMAT=(\d+):(\d+)", txt); idig, ddig = (int(m.group(1)), int(m.group(2))) if m else (4, 4)
    lz = "LZ" in txt; cur = None; out = []; x = y = 0
    def c(tok):
        neg = tok.startswith("-"); tok = tok.lstrip("+-")
        tok = tok.ljust(idig + ddig, "0") if lz else tok.rjust(idig + ddig, "0")
        return (-1 if neg else 1) * int(tok) * 10 ** -ddig
    for l in txt.splitlines():
        l = l.strip(); m = re.fullmatch(r"T(\d+)", l)
        if m: cur = str(int(m.group(1))); continue
        if cur and re.match(r"^[XY][-\d]", l):
            mx = re.search(r"X(-?\d+)", l); my = re.search(r"Y(-?\d+)", l)
            if mx: x = c(mx.group(1))
            if my: y = c(my.group(1))
            out.append((x, y, tools[cur]))
    return out


# ---------- Gerber 銅網路查詢（analysis 預先算好的點陣標記） ----------
sys.path.insert(0, ANA)
import raster  # noqa: E402
LAB = pickle.load(open(os.path.join(ANA, "lab.pkl"), "rb"))
LT, LB = np.load(os.path.join(ANA, "lt.npy")), np.load(os.path.join(ANA, "lb.npy"))
PAR, NT = LAB["par"], LAB["nt"]


def root(i):
    while PAR[i] != i: i = PAR[i]
    return i


def gnet(layer, gx, gy, search=6):
    a, b = raster.P(gx, gy); r, c = int(b), int(a)
    lab = LT if layer == "F" else LB
    v = int(lab[r, c]) if 0 <= r < lab.shape[0] and 0 <= c < lab.shape[1] else 0
    if not v:
        for rad in range(1, search + 1):
            w = lab[max(0, r - rad):r + rad + 1, max(0, c - rad):c + rad + 1]; nz = w[w > 0]
            if nz.size: v = int(np.bincount(nz).argmax()); break
    if not v: return None
    return root(v if layer == "F" else v + NT)


# ---------- 封裝載入與對位 ----------
def load_fp(fpid):
    lib, name = fpid.split(":")
    path = os.path.join(ROOT, "hardware", "lib", "WOOW.pretty") if lib == "WOOW" else f"/usr/share/kicad/footprints/{lib}.pretty"
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None: raise SystemExit(f"找不到封裝 {fpid}")
    return fp


def cpl():
    raw = open(os.path.join(GB, "Pick Place for WO_30109_Zigbee.csv"), "rb").read().decode("cp950")
    L = raw.splitlines(); hi = next(i for i, l in enumerate(L) if l.startswith('"Designator"'))
    out = {}
    for r in csv.DictReader(io.StringIO("\n".join(L[hi:]))):
        g = lambda k: float(re.sub(r"[^0-9.\-]", "", r[k])) * 0.0254
        ref = r["Designator"].strip().replace("Relay", "K")
        out[ref] = (g("Center-X(mil)"), g("Center-Y(mil)"), r["Layer"].strip() == "BottomLayer")
    return out


def pad_local(fp):
    """封裝在原點、0 度、正面時各焊盤的 (pad, 號碼, dx, dy[KiCad mm])"""
    o = fp.GetPosition()
    return [(p, p.GetNumber(), pcbnew.ToMM(p.GetPosition().x - o.x), pcbnew.ToMM(p.GetPosition().y - o.y)) for p in fp.Pads()]


def transform(dx, dy, rot, bottom):
    if bottom: dx = -dx                                   # 翻面：KiCad 以 Y 軸鏡射
    a = math.radians(rot)
    # KiCad 角度逆時針、Y 向下 -> 畫面上 (x,y) 旋轉
    return dx * math.cos(a) + dy * math.sin(a), -dx * math.sin(a) + dy * math.cos(a)


def sch_netlist():
    """以 kicad-cli 從原理圖匯出網表：{(ref, pin): 網路名稱}（含 unconnected-(...)）"""
    import subprocess, tempfile
    sch = os.path.join(PRJ, "WO30109_FreshAir.kicad_sch")
    tmp = tempfile.mktemp(suffix=".net")
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", tmp, sch],
                   check=True, capture_output=True)
    txt = open(tmp).read(); out = {}
    for m in re.finditer(r'\(net\s*\(code "\d+"\)\s*\(name "([^"]*)"\)(.*?)\n\t\t\)', txt, re.S):
        for r, pn in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', m.group(2)):
            out[(r, pn)] = m.group(1)
    return out


def main():
    P = design.build(VERSION)
    SN = sch_netlist()
    CP = cpl()
    lay = {"F": parse(os.path.join(GB, "WO_30109_Zigbee.GTL")), "B": parse(os.path.join(GB, "WO_30109_Zigbee.GBL"))}
    DR = drills()
    th_flash = [(x, y) for x, y, d in DR if d > 0.5]
    smd = {k: [(x, y) for x, y, t, p in lay[k][1]] for k in "FB"}

    board = pcbnew.NewBoard(OUT)
    # 板框
    corners = [K(-31.7, 0), K(31.7, 0), K(31.7, 83.08), K(-31.7, 83.08)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1)); s.SetStart(V(*a)); s.SetEnd(V(*b)); board.Add(s)
    # 網路
    netobj = {}
    def NF(full):                                    # 用原理圖網表的完整網路名稱
        if full not in netobj:
            ni = pcbnew.NETINFO_ITEM(board, full); board.Add(ni); netobj[full] = ni
        return netobj[full]

    def N(name):                                     # 原理圖用區域標籤 -> 網路名稱帶 "/" 前綴
        return NF("/" + name)
    for p in P.values():
        for n in p["pins"].values():
            if n: N(n)

    # 已知錨點：大片鋪銅=GND、SWD 排針 pin1=+3V3、C6 正端=+12V（量測自原板）
    seed = {}
    for sn, L, x, y in (("GND", "B", -7.55, 58.53), ("+3V3", "F", -12.95, 58.53), ("+12V", "B", 3.47, 44.93)):
        seed.setdefault(gnet(L, x, y), {})[sn] = 1000
    report = {"placed": {}, "unplaced": [], "conflicts": []}
    order = sorted(P, key=lambda r: -len(P[r]["pins"]))      # 腳多的先放（對位最可靠）
    fps = {}
    for passno in (1, 2):
      g2s = {k: dict(v) for k, v in seed.items()} if passno == 1 else {k: {max(v, key=v.get): 1000} for k, v in g2s.items()}
      for ref in order:
          part = P[ref]
          if ref in fps:
              fp = fps[ref]
              if fp.IsFlipped(): fp.SetPosition(V(0, 0)); fp.Flip(V(0, 0), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
              fp.SetOrientationDegrees(0)
          else:
              fp = load_fp(part["footprint"]); fps[ref] = fp
              lib, name = part["footprint"].split(":"); fp.SetFPID(pcbnew.LIB_ID(lib, name))
              fp.SetReference(ref); fp.SetValue(part["value"])
              board.Add(fp)
          fp.SetField("LCSC", part["lcsc"] or "")
          if ref in design.HOLES:                       # 固定孔：直接放原位
              fp.SetPosition(V(*K(*design.HOLES[ref]))); report["placed"][ref] = dict(rot=0, bottom=False, rms=0)
              continue
          if ref not in CP:
              report["unplaced"].append(ref); continue
          cx, cy, bottom = CP[ref]
          pads0 = list(fp.Pads())
          is_th = any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in pads0)
          cand = th_flash if is_th else smd["B" if bottom else "F"]
          near = [(x, y) for x, y in cand if abs(x - cx) < 30 and abs(y - cy) < 30]
          if bottom:
              fp.SetPosition(V(0, 0)); fp.Flip(V(0, 0), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
          best = None
          for rot in (0, 90, 180, 270):
              fp.SetPosition(V(0, 0)); fp.SetOrientationDegrees(rot)
              tl = [(pd.GetNumber(), pcbnew.ToMM(pd.GetPosition().x), pcbnew.ToMM(pd.GetPosition().y)) for pd in fp.Pads()]
              for fx, fy in near:
                  kx, ky = K(fx, fy)
                  ox, oy = kx - tl[0][1], ky - tl[0][2]
                  gx0, gy0 = ox - OX - BW / 2, OY + BH - oy
                  err = 0; nets = []
                  for n, tx, ty in tl:
                      gx, gy = ox + tx - OX - BW / 2, OY + BH - (oy + ty)
                      d = min(math.hypot(gx - x, gy - y) for x, y in near) if near else 9
                      err += min(d, 3) ** 2
                      nets.append((part["pins"].get(n), gnet("B" if bottom and not is_th else "F", gx, gy)))
                  pen = sum(4 for sn, gn in nets if sn and gn is not None and g2s.get(gn) and sn not in g2s[gn])
                  for (s1, g1), (s2, g2) in itertools.combinations(nets, 2):
                      if s1 and s2 and g1 is not None and g2 is not None and (s1 == s2) != (g1 == g2): pen += 2
                  score = err + pen + 0.01 * math.hypot(gx0 - cx, gy0 - cy)
                  if best is None or score < best[0]:
                      best = (score, rot, ox, oy, nets, err)
          locs = tl
          score, rot, ox, oy, nets, err = best
          fp.SetOrientationDegrees(rot)
          fp.SetPosition(V(ox, oy))
          for sn, gn in nets:
              if sn and gn is not None: g2s.setdefault(gn, {}).setdefault(sn, 0); g2s[gn][sn] += 1
          report["placed"][ref] = dict(rot=rot, bottom=bottom, rms=round(math.sqrt(err / max(1, len(locs))), 3))
          for pad in fp.Pads():
              full = SN.get((ref, pad.GetNumber()))
              if full: pad.SetNet(NF(full))

    # 檢查：同一 Gerber 網路對到多個原理圖網路
    for gn, d in g2s.items():
        if len(d) > 1: report["conflicts"].append(sorted(d))
    gmap = {gn: max(d, key=d.get) for gn, d in g2s.items()}

    # 走線與過孔
    nt = nv = un = 0
    for L, layer in (("F", pcbnew.F_Cu), ("B", pcbnew.B_Cu)):
        for x1, y1, x2, y2, w in lay[L][0]:
            t = pcbnew.PCB_TRACK(board); t.SetStart(V(*K(x1, y1))); t.SetEnd(V(*K(x2, y2)))
            t.SetWidth(MM(w)); t.SetLayer(layer)
            gn = gnet(L, (x1 + x2) / 2, (y1 + y2) / 2)
            if gn in gmap: t.SetNet(N(gmap[gn]))
            else: un += 1
            board.Add(t); nt += 1
    for x, y, d in DR:
        if d > 0.45: continue
        v = pcbnew.PCB_VIA(board); v.SetPosition(V(*K(x, y))); v.SetDrill(MM(d)); v.SetWidth(MM(0.6))
        gn = gnet("F", x, y) or gnet("B", x, y)
        if gn in gmap: v.SetNet(N(gmap[gn]))
        board.Add(v); nv += 1
    if VERSION.startswith("2"):
        import upgrade_v20_pcb
        info = upgrade_v20_pcb.apply(board, fps, P, SN, NF, load_fp)
        report["v2"] = info
    # 鋪銅：兩面 GND（V1.3 原設計間隙 0.127mm；V2.0 由 .kicad_dru 規則決定退讓距離）
    zc = float(os.environ.get("ZONE_CLEARANCE", "0.127"))
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board); z.SetLayer(layer); z.SetNet(N("GND"))
        z.SetLocalClearance(MM(zc)); z.SetMinThickness(MM(0.127))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetThermalReliefGap(MM(0.3)); z.SetThermalReliefSpokeWidth(MM(0.4))
        ol = z.Outline(); ol.NewOutline()
        for gx, gy in ((-31.45, 0.25), (31.45, 0.25), (31.45, 82.83), (-31.45, 82.83)):
            ol.Append(V(*K(gx, gy)))
        board.Add(z)
    pcbnew.SaveBoard(OUT, board)
    dru = os.path.splitext(OUT)[0] + ".kicad_dru"
    hide = os.environ.get("ZONE_RULES", "1") == "0" and os.path.exists(dru)
    if hide: os.rename(dru, dru + ".off")          # V1.3 重現原板：鋪銅不套安規規則（照原本 0.127mm）
    try:
        board = pcbnew.LoadBoard(OUT)                # 從檔案重新載入，套用 .kicad_pro 網路類別（與 .kicad_dru）
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        pcbnew.SaveBoard(OUT, board)
    finally:
        if hide: os.rename(dru + ".off", dru)
    report.update(tracks=nt, vias=nv, tracks_without_net=un)
    json.dump(report, open(os.path.join(os.path.dirname(OUT), "import_report.json"), "w"), ensure_ascii=False, indent=1)
    worst = sorted(report["placed"].items(), key=lambda kv: -kv[1]["rms"])[:6]
    print(f"placed {len(report['placed'])}, unplaced {report['unplaced']}, tracks {nt} (no-net {un}), vias {nv}")
    print("worst pad fit (rms mm):", [(k, v['rms']) for k, v in worst])
    print("net conflicts:", report["conflicts"])


if __name__ == "__main__":
    main()
