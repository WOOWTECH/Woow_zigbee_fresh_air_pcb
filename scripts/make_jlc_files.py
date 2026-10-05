"""產生 JLCPCB 下單檔：Gerber＋鑽孔 zip、BOM、CPL（座標），輸出到 hardware/WO30109_FreshAir/output/jlc/。

  python3 scripts/make_jlc_files.py [--rotations cpl_rotations_db.csv] [--refresh-footprints] [--check]

CPL 的角度要換成 JLC 的零度方向，否則貼片會轉錯。
  * 位置 = 所有焊盤外框的中心（不是封裝原點），Y 取負
  * 角度優先用「JLC 實際用的封裝」算：jlc_footprints.json 存了每個 LCSC 料號在 EasyEDA 庫裡的焊盤座標，
    依焊盤編號試 0/90/180/270，找讓它們落在 KiCad 板上焊盤的角度（容差 0.35mm）。
    正面：板上 = R(θ)·E；背面：板上 = MirrorX·R(θ)·E（2026-10-05 在 JLC 3D 檢視器用 D1、Q1、U4 實測確認）。
  * 算不出唯一答案（對稱件、焊盤編號對不上、快取裡沒有）才退回 kicad-jlcpcb-tools（Bouni）外掛的做法：
    背面先 (180 - θ) % 360，再加 matthewlai/JLCKicadTools cpl_rotations_db.csv 的修正
    （正規表示式，依序比對 位號、值、封裝名，取匹配最長的一條），偏移依角度旋轉後加上去。
為什麼不能只靠規則表：規則表依「封裝名稱」比對，但 JLC 擺件用的是該料號自己的 EasyEDA 封裝，
同叫 SOT-23 的料，方向可能不同。V3.0 的 Q1–Q4（C5224182）、U4（C780769）、S1（C7421516）
用規則表算都差 90°。
料號換了或新增，要跑 --refresh-footprints（需要 pip install easyeda2kicad）更新快取。
"""
import argparse, csv, glob, io, itertools, json, math, os, re, shutil, subprocess, sys, tempfile, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PRJ = os.path.join(ROOT, "hardware", "WO30109_FreshAir")
NAME = "WO30109_FreshAir"
OUT = os.path.join(PRJ, "output", "jlc")
DB_URL = "https://raw.githubusercontent.com/matthewlai/JLCKicadTools/master/jlc_kicad_tools/cpl_rotations_db.csv"
FOOTPRINTS = os.path.join(PRJ, "jlc_footprints.json")
TOLERANCE = 0.35                                        # mm，焊盤中心最大誤差


def _rot(p, t):
    """KiCad 方向的旋轉（Y 向下、正角度＝畫面上逆時針）。"""
    a = math.radians(t)
    return (p[0] * math.cos(a) + p[1] * math.sin(a), -p[0] * math.sin(a) + p[1] * math.cos(a))


def _centroids(pads):
    """{編號: [(x, y), ...]} → {編號: 中心}；同編號多個焊盤（散熱焊盤陣列、按鍵兩腳）取平均。"""
    return {n: (sum(p[0] for p in v) / len(v), sum(p[1] for p in v) / len(v)) for n, v in pads.items() if v}


def solve_rotation(board_pads, jlc_pads, bottom):
    """回傳讓 JLC 封裝焊盤落在板上焊盤的角度清單（0/90/180/270，容差內全部列出）與最小誤差。
    board_pads、jlc_pads：{焊盤編號: [(x, y), ...]}，board 用板上絕對座標（KiCad，Y 向下）。
    焊盤多（≥10 個編號）時容許 10% 對不上：模組的散熱焊盤在 EasyEDA 常被拆成好幾個編號（ESP32-C6 的 29–37）。
    清單只有一個角度才算唯一解；空清單＝對不上，多個＝對稱件，兩種都要退回規則表。"""
    kb, kj = _centroids(board_pads), _centroids(jlc_pads)
    common = sorted(set(kb) & set(kj))
    if len(common) < 2:
        return [], math.inf
    allowed_misses = len(common) // 10
    def centre(c, names):
        xs = [c[n][0] for n in names]; ys = [c[n][1] for n in names]
        return ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    def worst(t, names):
        cb, cj = centre(kb, names), centre(kj, names)
        err = 0.0
        for n in names:
            e = _rot((kj[n][0] - cj[0], kj[n][1] - cj[1]), t)
            if bottom:
                e = (-e[0], e[1])
            err = max(err, math.hypot(kb[n][0] - cb[0] - e[0], kb[n][1] - cb[1] - e[1]))
        return err
    hits, best = [], math.inf
    for t in (0, 90, 180, 270):
        # 依序試剔除 0、1、…、allowed_misses 個焊盤（離群的焊盤會把中心拉歪，所以要整組重算）
        err = math.inf
        for k in range(allowed_misses + 1):
            for drop in itertools.islice(itertools.combinations(common, k), 2000):
                err = min(err, worst(t, [n for n in common if n not in drop]))
            if err <= TOLERANCE:
                break
        best = min(best, err)
        if err <= TOLERANCE:
            hits.append(t)
    return hits, best


def load_footprints():
    return json.load(open(FOOTPRINTS)) if os.path.exists(FOOTPRINTS) else {}


def refresh_footprints(lcsc_codes):
    """用 easyeda2kicad 下載每個料號的 EasyEDA 封裝，只留焊盤座標寫進 jlc_footprints.json。"""
    if not shutil.which("easyeda2kicad"):
        sys.exit("需要 easyeda2kicad：pip install easyeda2kicad")
    db = load_footprints()
    for code in sorted(lcsc_codes):
        tmp = tempfile.mkdtemp()
        r = subprocess.run(["easyeda2kicad", "--footprint", f"--lcsc_id={code}", f"--output={tmp}/lib"],
                           capture_output=True, text=True)
        files = glob.glob(f"{tmp}/lib.pretty/*.kicad_mod")
        if not files:                                   # EasyEDA 連續抓約 20 筆會回 403，稍後重跑即可
            print(f"  {code}：下載失敗 {r.stdout.strip().splitlines()[-1:] or ''}")
            continue
        text = open(files[0]).read()
        pads = {}
        for m in re.finditer(r'\(pad\s+"?(\w+)"?\s+(?:smd|thru_hole)\s+\w+\s+\(at ([-\d.]+) ([-\d.]+)', text):
            pads.setdefault(m.group(1), []).append([float(m.group(2)), float(m.group(3))])
        db[code] = {"footprint": os.path.basename(files[0])[:-len(".kicad_mod")], "pads": pads}
        print(f"  {code}：{db[code]['footprint']}（{len(pads)} 個焊盤編號）")
        shutil.rmtree(tmp)
    json.dump(db, open(FOOTPRINTS, "w"), indent=1, ensure_ascii=False, sort_keys=True)


def load_rules(path):
    text = open(path).read() if path else urllib.request.urlopen(DB_URL, timeout=30).read().decode()
    rules = []
    for row in list(csv.reader(io.StringIO(text)))[1:]:
        if not row or not row[0].strip():
            continue
        ox = float(row[2]) if len(row) > 2 and row[2].strip() else 0.0
        oy = float(row[3]) if len(row) > 3 and row[3].strip() else 0.0
        rules.append((row[0], re.compile(row[0]), int(float(row[1])), (ox, oy)))
    return rules


def match(rules, *targets):
    for t in targets:                                   # 位號 → 值 → 封裝名，先命中的那一層為準
        best, best_len = None, -1
        for r in rules:
            m = r[1].search(t)
            if m and len(m.group(0)) > best_len:
                best, best_len = r, len(m.group(0))
        if best:
            return best
    return None


def gerbers():
    g = os.path.join(OUT, "gerber")
    os.makedirs(g, exist_ok=True)
    pcb = os.path.join(PRJ, NAME + ".kicad_pcb")
    layers = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"
    subprocess.run(["kicad-cli", "pcb", "export", "gerbers", "--layers", layers, "--subtract-soldermask",
                    "--no-x2", "-o", g + "/", pcb], check=True, capture_output=True)
    subprocess.run(["kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--excellon-separate-th",
                    "--generate-map", "--map-format", "gerberx2", "-o", g + "/", pcb], check=True, capture_output=True)
    z = os.path.join(OUT, f"GERBER-{NAME}.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as f:
        for n in sorted(os.listdir(g)):
            f.write(os.path.join(g, n), n)
    return z


def bom_and_cpl(rules, footprints):
    import pcbnew
    b = pcbnew.LoadBoard(os.path.join(PRJ, NAME + ".kicad_pcb"))
    groups, cpl, hand, unruled = {}, [], [], []
    for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
        ref, val = fp.GetReference(), fp.GetValue()
        fpname = str(fp.GetFPID().GetLibItemName())
        lcsc = fp.GetFieldText("LCSC") if fp.HasField("LCSC") else ""
        if fp.IsExcludedFromBOM():
            continue
        if not lcsc:
            hand.append(f"{ref}（{val}）")
            continue
        groups.setdefault((val, fpname, lcsc), []).append(ref)
        if fp.IsExcludedFromPosFiles():
            continue
        pads = list(fp.Pads())
        bbox = pads[0].GetBoundingBox()
        for p in pads:
            bbox.Merge(p.GetBoundingBox())
        c = bbox.GetCenter()
        x, y = pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
        bottom = fp.GetLayer() != pcbnew.F_Cu
        kicad_rot = fp.GetOrientation().AsDegrees()
        rot = (180 - kicad_rot) % 360 if bottom else kicad_rot
        r = match(rules, ref, val, fpname)
        rule_rot = (rot + r[2] % 360) % 360 if r else rot
        board_pads = {}
        for p in pads:
            q = p.GetPosition()
            board_pads.setdefault(p.GetNumber(), []).append((pcbnew.ToMM(q.x), pcbnew.ToMM(q.y)))
        hits, err = solve_rotation(board_pads, footprints[lcsc]["pads"], bottom) if lcsc in footprints else ([], None)
        if len(hits) == 1:                              # JLC 封裝算出唯一解：以它為準
            rot, how = hits[0], f"JLC 封裝 {footprints[lcsc]['footprint']}"
            if rule_rot % 360 != rot:
                how += f"（規則表會給 {rule_rot % 360:.0f}°，已改正）"
        else:
            rot = rule_rot
            if r:
                ox, oy = r[3]
                if ox or oy:
                    a = math.radians(rot)
                    dx, dy = ox * math.cos(a) + oy * math.sin(a), -ox * math.sin(a) + oy * math.cos(a)
                    x, y = x + (-dx if bottom else dx), y + dy
            why = "沒有封裝快取" if err is None else ("對稱件" if hits else f"焊盤對不上（誤差 {err:.2f}mm）")
            how = f"規則 {r[0]}（{why}）" if r else ""
            if not r:
                unruled.append(f"{ref}（{why}）")
        cpl.append([ref, f"{x:.4f}mm", f"{-y:.4f}mm", "Bottom" if bottom else "Top", f"{rot % 360:.0f}", how])
    with open(os.path.join(OUT, f"BOM-{NAME}.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, fpname, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([val, ",".join(refs), fpname, lcsc])
    with open(os.path.join(OUT, f"CPL-{NAME}.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in cpl:
            w.writerow(row[:5])
    return groups, cpl, hand, unruled


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rotations", help="本機的 cpl_rotations_db.csv（預設從 GitHub 下載最新版）")
    ap.add_argument("--check", action="store_true",
                    help="有料號不在 jlc_footprints.json 時以錯誤結束（CI 用，提醒換料後要 --refresh-footprints）")
    ap.add_argument("--refresh-footprints", action="store_true",
                    help="重新下載 BOM 所有料號的 EasyEDA 封裝，更新 jlc_footprints.json")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    rules = load_rules(args.rotations)
    if args.refresh_footprints:
        import pcbnew
        b = pcbnew.LoadBoard(os.path.join(PRJ, NAME + ".kicad_pcb"))
        refresh_footprints({f.GetFieldText("LCSC") for f in b.GetFootprints()
                            if f.HasField("LCSC") and f.GetFieldText("LCSC")})
    footprints = load_footprints()
    z = gerbers()
    groups, cpl, hand, unruled = bom_and_cpl(rules, footprints)
    missing = sorted({lcsc for (_, _, lcsc) in groups} - set(footprints))
    print(f"Gerber：{os.path.relpath(z, ROOT)}（{os.path.getsize(z) // 1024} KB）")
    print(f"BOM：{len(groups)} 種、{sum(len(v) for v in groups.values())} 顆；CPL：{len(cpl)} 顆"
          f"（正面 {sum(r[3] == 'Top' for r in cpl)}、背面 {sum(r[3] == 'Bottom' for r in cpl)}）")
    print("角度來源：")
    for row in cpl:
        if row[5]:
            print(f"  {row[0]:5s} {row[3]:6s} {row[4]:>4s}°  {row[5]}")
    if missing:
        print("⚠️ jlc_footprints.json 沒有這些料號，角度只靠規則表（跑 --refresh-footprints）：", "、".join(missing))
    print("手焊／不貼（無 LCSC）：", "、".join(hand))
    if unruled:
        print("⚠️ 角度沒有經過驗證（下單時在 JLC 3D 預覽逐顆確認）：", "、".join(unruled))
    if args.check and missing:
        return 1


if __name__ == "__main__":
    sys.exit(main())
