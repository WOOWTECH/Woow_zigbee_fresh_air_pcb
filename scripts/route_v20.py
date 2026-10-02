"""V2.0 第 2 段：低壓網路交給 Freerouting，再鋪銅。

  python3 route_v20.py            # 需 java、xvfb-run、FREEROUTING_JAR（預設 ~/eda-tools/freerouting/freerouting-1.9.0.jar）

市電網路已由 upgrade_v20_pcb.py / mains_router.py 繞好並鎖定。匯出 DSN 時把 MAINS 類別間距改成 6.4mm，
Freerouting 繞低壓線就會自動離市電 6.4mm 以上。
每個 pcbnew 步驟各開一個 Python 行程（KiCad 10 的 SWIG 綁定在同一行程多次 LoadBoard 後偶爾會回傳無型別物件）。
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.abspath(os.path.join(HERE, "..", "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb"))
JAR = os.environ.get("FREEROUTING_JAR", os.path.expanduser("~/eda-tools/freerouting/freerouting-1.9.0.jar"))
PASSES = os.environ.get("FR_PASSES", "40")
OX, OY, BW, BH = 100.0, 100.0, 63.4, 83.08


def step(name, *args):
    """在新的 Python 行程執行本檔的某個步驟"""
    r = subprocess.run([sys.executable, __file__, name, *args], capture_output=True, text=True)
    out = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if "assert" not in l and "property.h" not in l and "memory leak" not in l and "Debug" not in l)
    if r.returncode != 0:
        sys.exit(f"[{name}] 失敗：\n{out}")
    return out


def do_strip(dsn):
    import pcbnew
    b = pcbnew.LoadBoard(PCB)
    for z in list(b.Zones()):                    # 鋪銅先拿掉：讓 Freerouting 把 GND 也當一般網路繞通
        if not z.GetIsRuleArea(): b.Remove(z)
    pcbnew.SaveBoard(PCB, b)


def do_export(dsn):
    import pcbnew
    b = pcbnew.LoadBoard(PCB)
    assert pcbnew.ExportSpecctraDSN(b, dsn)
    s = open(dsn).read()
    i = s.index("(class MAINS"); j = s.index("(clearance", i); k = s.index(")", j)
    open(dsn, "w").write(s[:j] + "(clearance 6400" + s[k:])


def do_import(ses):
    import pcbnew
    b = pcbnew.LoadBoard(PCB)
    assert pcbnew.ImportSpecctraSES(b, ses)
    pcbnew.SaveBoard(PCB, b)


def do_zones(_):
    import pcbnew
    b = pcbnew.LoadBoard(PCB)
    net = b.FindNet("/GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(net)
        z.SetLocalClearance(pcbnew.FromMM(0.2)); z.SetMinThickness(pcbnew.FromMM(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        ol = z.Outline(); ol.NewOutline()
        for gx, gy in ((-31.2, 0.5), (31.2, 0.5), (31.2, 82.58), (-31.2, 82.58)):
            ol.Append(pcbnew.VECTOR2I(pcbnew.FromMM(OX + gx + BW / 2), pcbnew.FromMM(OY + BH - gy)))
        b.Add(z)
    pcbnew.SaveBoard(PCB, b)


def do_fill(_):
    import pcbnew
    b = pcbnew.LoadBoard(PCB)                    # 從檔案載入才會套用 .kicad_dru，鋪銅自動離市電 6.4mm
    for fp in b.GetFootprints():                 # LCSC 料號只給 BOM 用，不印在絲印上
        for f in fp.GetFields():
            if f.GetName() == "LCSC": f.SetVisible(False)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    pcbnew.SaveBoard(PCB, b)


def main():
    work = tempfile.mkdtemp(prefix="fr_")
    dsn, ses, log = (os.path.join(work, f) for f in ("board.dsn", "board.ses", "fr.log"))
    step("strip", dsn); step("export", dsn)
    subprocess.run(["xvfb-run", "-a", "java", "-jar", JAR, "-de", dsn, "-do", ses, "-mp", PASSES],
                   stdout=open(log, "w"), stderr=subprocess.STDOUT, timeout=3600)
    if not os.path.exists(ses):
        sys.exit(f"Freerouting 失敗，見 {log}")
    step("import", ses); step("zones", ""); step("fill", "")
    print("routed ->", PCB, "| log:", log)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        {"strip": do_strip, "export": do_export, "import": do_import, "zones": do_zones, "fill": do_fill}[sys.argv[1]](sys.argv[2] if len(sys.argv) > 2 else "")
    else:
        main()
