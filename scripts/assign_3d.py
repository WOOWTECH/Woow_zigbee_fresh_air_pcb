"""把 WOOW 自訂封裝的 3D 模型路徑寫進封裝庫與板子（模型由 make_3d_models.py 產生）。"""
import os, glob, re
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
PCB = os.path.join(ROOT, "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb")
MODEL = "${KIPRJMOD}/../lib/WOOW.3dshapes/%s.step"
have = {os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(ROOT, "hardware/lib/WOOW.3dshapes/*.step"))}
# 1) 封裝庫：替換或加上 (model ...) 區塊
for path in glob.glob(os.path.join(ROOT, "hardware/lib/WOOW.pretty/*.kicad_mod")):
    name = os.path.splitext(os.path.basename(path))[0]
    if name not in have: continue
    s = open(path).read()
    s = re.sub(r'\n\t\(model .*?\n\t\)', '', s, flags=re.S)                      # KiCad 10 多行格式
    s = re.sub(r'\t\(model "[^"]*"[^\n]*\n(\t\t[^\n]*\n)*?\t\)\n', '', s)
    blk = '\t(model "%s"\n\t\t(offset (xyz 0 0 0))\n\t\t(scale (xyz 1 1 1))\n\t\t(rotate (xyz 0 0 0))\n\t)\n' % (MODEL % name)
    s = s.rstrip().rstrip(')').rstrip() + '\n\n' + blk + ')\n'   # 固定一行空行：重跑不會越疊越多
    if s != open(path).read():
        open(path, 'w').write(s)
# 2) 板子上的封裝
b = pcbnew.LoadBoard(PCB); n = 0
for fp in b.GetFootprints():
    name = fp.GetFPID().GetLibItemName().wx_str() if hasattr(fp.GetFPID().GetLibItemName(), 'wx_str') else str(fp.GetFPID().GetLibItemName())
    if fp.GetFPID().GetLibNickname() == "WOOW" and name in have:
        fp.Models().clear()
        m = pcbnew.FP_3DMODEL(); m.m_Filename = MODEL % name; fp.Models().append(m); n += 1
pcbnew.SaveBoard(PCB, b)
print(f"3D 模型：封裝庫 {len(have)} 個、板上 {n} 顆零件")
