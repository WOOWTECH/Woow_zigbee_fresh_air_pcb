"""產生 JLCPCB 下單檔：Gerber＋鑽孔 zip、BOM、CPL（座標），輸出到 hardware/WO30109_FreshAir/output/jlc/。

  python3 scripts/make_jlc_files.py [--rotations cpl_rotations_db.csv]

CPL 的角度要換成 JLC 的零度方向，否則貼片會轉錯。這裡照 kicad-jlcpcb-tools（Bouni）外掛的做法：
  * 位置 = 所有焊盤外框的中心（不是封裝原點），Y 取負
  * 背面：角度先 (180 - θ) % 360
  * 修正規則 = matthewlai/JLCKicadTools 的 cpl_rotations_db.csv（正規表示式），依序比對 位號、值、封裝名，
    取匹配長度最長的一條（同長度取先出現的），角度加上去、偏移依角度旋轉後加上去
規則表沒涵蓋的自訂封裝（WOOW:*）印在最後，要在 JLC 下單頁的 3D 預覽逐顆確認。
"""
import argparse, csv, io, math, os, re, subprocess, sys, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PRJ = os.path.join(ROOT, "hardware", "WO30109_FreshAir")
NAME = "WO30109_FreshAir"
OUT = os.path.join(PRJ, "output", "jlc")
DB_URL = "https://raw.githubusercontent.com/matthewlai/JLCKicadTools/master/jlc_kicad_tools/cpl_rotations_db.csv"


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


def bom_and_cpl(rules):
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
        rot = fp.GetOrientation().AsDegrees()
        if bottom:
            rot = (180 - rot) % 360
        r = match(rules, ref, val, fpname)
        if r:
            rot = (rot + r[2] % 360) % 360
            ox, oy = r[3]
            if ox or oy:
                a = math.radians(rot)
                dx, dy = ox * math.cos(a) + oy * math.sin(a), -ox * math.sin(a) + oy * math.cos(a)
                x, y = x + (-dx if bottom else dx), y + dy
        elif fp.GetFPID().GetLibNickname() == "WOOW":
            unruled.append(ref)
        cpl.append([ref, f"{x:.4f}mm", f"{-y:.4f}mm", "Bottom" if bottom else "Top", f"{rot:.0f}",
                    r[0] if r else ""])
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
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    rules = load_rules(args.rotations)
    z = gerbers()
    groups, cpl, hand, unruled = bom_and_cpl(rules)
    print(f"Gerber：{os.path.relpath(z, ROOT)}（{os.path.getsize(z) // 1024} KB）")
    print(f"BOM：{len(groups)} 種、{sum(len(v) for v in groups.values())} 顆；CPL：{len(cpl)} 顆"
          f"（正面 {sum(r[3] == 'Top' for r in cpl)}、背面 {sum(r[3] == 'Bottom' for r in cpl)}）")
    print("旋轉修正：")
    for row in cpl:
        if row[5]:
            print(f"  {row[0]:5s} {row[3]:6s} {row[4]:>4s}°  規則 {row[5]}")
    print("手焊／不貼（無 LCSC）：", "、".join(hand))
    if unruled:
        print("⚠️ 自訂封裝、無旋轉規則（下單時在 JLC 3D 預覽逐顆確認）：", "、".join(unruled))


if __name__ == "__main__":
    sys.exit(main())
