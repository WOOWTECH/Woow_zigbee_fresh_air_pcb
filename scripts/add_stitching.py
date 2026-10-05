"""GND 縫合過孔：上下兩層 GND 鋪銅都覆蓋的位置，每 PITCH mm 一顆（0.6/0.3mm）。
只放在過孔周圍 R mm 一圈都是 GND 鋪銅的地方 → 不會碰到其他網路，也自然避開市電安全帶。
用法：python3 add_stitching.py [pitch_mm]（之後要重新鋪銅、跑 DRC）
"""
import math, os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.abspath(os.path.join(HERE, "..", "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb"))
PITCH = float(next((a for a in sys.argv[1:] if not a.startswith("-")), 3.0))
R = 0.75                                        # 過孔半徑 0.3 + 間距餘量
# 只在指定範圍加（KiCad mm：x0,y0,x1,y1），例如 V3.2 只補新增 DI 區域，不動板子其他地方
REGION = tuple(map(float, os.environ["STITCH_REGION"].split(","))) if os.environ.get("STITCH_REGION") else None
def in_region(x, y):
    return REGION is None or (REGION[0] <= x <= REGION[2] and REGION[1] <= y <= REGION[3])
b = pcbnew.LoadBoard(PCB)
gnd = b.FindNet("/GND")
zones = {L: [z for z in b.Zones() if z.GetNetCode() == gnd.GetNetCode() and z.IsOnLayer(L)] for L in (pcbnew.F_Cu, pcbnew.B_Cu)}
def covered(x, y):
    for L, zs in zones.items():
        for dx, dy in [(0, 0)] + [(R * math.cos(a), R * math.sin(a)) for a in [i * math.pi / 4 for i in range(8)]]:
            p = pcbnew.VECTOR2I(pcbnew.FromMM(x + dx), pcbnew.FromMM(y + dy))
            if not any(z.HitTestFilledArea(L, p) for z in zs): return False
    return True



def add():
    bb = b.GetBoardEdgesBoundingBox()
    x0, y0, x1, y1 = (pcbnew.ToMM(v) for v in (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))
    n = 0; y = y0 + 1.5
    while y < y1 - 1.0:
        x = x0 + 1.5
        while x < x1 - 1.0:
            if in_region(x, y) and covered(x, y):
                v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
                v.SetDrill(pcbnew.FromMM(0.3)); v.SetWidth(pcbnew.FromMM(0.6)); v.SetNet(gnd); b.Add(v); n += 1
            x += PITCH
        y += PITCH
    pcbnew.SaveBoard(PCB, b)
    print(f"縫合過孔：新增 {n} 顆（間距 {PITCH}mm）")


def near_signal_vias(dist=0.95):
    """每個訊號過孔旁 dist mm 內補一顆 GND 過孔（EMC RP-001：換層回流路徑）"""
    sig = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetname() != "/GND"
           and in_region(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y))]
    n = miss = 0
    for v0 in sig:
        cx, cy = pcbnew.ToMM(v0.GetPosition().x), pcbnew.ToMM(v0.GetPosition().y)
        ok = False
        for r in (dist, dist - 0.15, dist + 0.2):
            for i in range(16):
                a = i * math.pi / 8
                x, y = cx + r * math.cos(a), cy + r * math.sin(a)
                if covered(x, y):
                    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
                    v.SetDrill(pcbnew.FromMM(0.3)); v.SetWidth(pcbnew.FromMM(0.6)); v.SetNet(gnd); b.Add(v)
                    n += 1; ok = True; break
            if ok: break
        if not ok: miss += 1
    pcbnew.SaveBoard(PCB, b)
    print(f"訊號過孔旁 GND 過孔：新增 {n} 顆，找不到空位 {miss} 顆（共 {len(sig)} 個訊號過孔）")


def prune_hole_to_hole():
    """移除 DRC 報 hole_to_hole 的縫合過孔（由 build 流程在鋪銅、DRC 後呼叫）"""
    import json, subprocess, tempfile
    out = tempfile.mktemp(suffix=".json")
    subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "-o", out, PCB], capture_output=True)
    bad = []
    for v in json.load(open(out))["violations"]:
        if v["type"] != "hole_to_hole": continue
        for it in v["items"]:
            if it["description"].startswith("Via [/GND]"): bad.append((it["pos"]["x"], it["pos"]["y"])); break
    b2 = pcbnew.LoadBoard(PCB); n = 0
    for t in list(b2.GetTracks()):
        if t.GetClass() == "PCB_VIA" and t.GetNetname() == "/GND":
            p = t.GetPosition()
            if any(abs(pcbnew.ToMM(p.x) - x) < 0.01 and abs(pcbnew.ToMM(p.y) - y) < 0.01 for x, y in bad):
                b2.Remove(t); n += 1
    pcbnew.SaveBoard(PCB, b2); print(f"移除孔距過近的縫合過孔 {n} 顆")


def stitch_islands():
    """沒接到另一層的 GND 鋪銅孤島（窄、3mm 格點與 0.75mm 半徑的 covered() 放不進去）：
    在孤島多邊形內部、上下兩層都是 GND、離既有 GND 過孔 >1mm 的點打過孔，每個孤島最多 3 顆"""
    b2 = b; g = gnd.GetNetCode()                 # 用模組載好的那份：同一行程再 LoadBoard 會拿到無型別物件
    links = [t.GetPosition() for t in b2.GetTracks() if t.GetClass() == 'PCB_VIA' and t.GetNetCode() == g]
    links += [p.GetPosition() for fp in b2.GetFootprints() for p in fp.Pads() if p.GetNetCode() == g and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
    zs = {L: [z for z in b2.Zones() if z.GetNetCode() == g and not z.GetIsRuleArea() and z.IsOnLayer(L)] for L in (pcbnew.F_Cu, pcbnew.B_Cu)}
    added = []
    for L, other in ((pcbnew.B_Cu, pcbnew.F_Cu), (pcbnew.F_Cu, pcbnew.B_Cu)):
        for z in zs[L]:
            poly = z.GetFilledPolysList(L)
            for i in range(poly.OutlineCount()):
                isl = poly.Outline(i)
                if any(isl.PointInside(p) for p in links): continue
                def ok(x, y, R=0.5):
                    for dx, dy in [(0, 0)] + [(R * math.cos(a), R * math.sin(a)) for a in [k * math.pi / 8 for k in range(16)]]:
                        p = pcbnew.VECTOR2I(pcbnew.FromMM(x + dx), pcbnew.FromMM(y + dy))
                        if not isl.PointInside(p) or not any(zz.HitTestFilledArea(other, p) for zz in zs[other]): return False
                    return all(math.hypot(x - pcbnew.ToMM(q.x), y - pcbnew.ToMM(q.y)) > 1.0 for q in links)
                bb = isl.BBox(); pts = []
                for yi in range(int(pcbnew.ToMM(bb.GetY()) * 20), int(pcbnew.ToMM(bb.GetBottom()) * 20)):
                    for xi in range(int(pcbnew.ToMM(bb.GetX()) * 20), int(pcbnew.ToMM(bb.GetRight()) * 20)):
                        if ok(xi / 20, yi / 20): pts.append((xi / 20, yi / 20))
                pick = []
                for p in pts:
                    if all(math.hypot(p[0] - q[0], p[1] - q[1]) > 5 for q in pick): pick.append(p)
                print(f"  孤島 {'B' if L == pcbnew.B_Cu else 'F'}.Cu {isl.Area() / 1e12:.1f}mm²：可打 {len(pts)} 點，取 {pick[:3]}")
                added += pick[:3]
    for x, y in added:
        v = pcbnew.PCB_VIA(b2); v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        v.SetDrill(pcbnew.FromMM(0.3)); v.SetWidth(pcbnew.FromMM(0.6)); v.SetNet(gnd); b2.Add(v)
    pcbnew.SaveBoard(PCB, b2); print(f"  孤島縫合過孔 {len(added)} 顆")


if __name__ == "__main__":
    if "--prune" in sys.argv: prune_hole_to_hole()
    elif "--islands" in sys.argv: stitch_islands()
    elif "--near-signal" in sys.argv: near_signal_vias()
    else: add()
