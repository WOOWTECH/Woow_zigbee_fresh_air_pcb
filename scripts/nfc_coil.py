"""NFC 印刷線圈的電感與調諧電容：讀 WOOW.pretty 封裝裡的實際銅箔線段，用部分電感法計算
（直線段自感 Grover 公式＋平行段互感數值積分；與矩形單匝公式解相差 0.1–0.2%）。

  python3 scripts/nfc_coil.py [封裝名]

ST25DV04KC 內建調諧電容 28.5pF（規格書 Features）；外加電容 C22 並聯在 AC0–AC1。
裝進外殼、手機靠近時諧振會往下掉幾百 kHz，所以目標調在 13.56MHz 以上一點，旁邊留 C24 空位微調。
"""
import math, os, re, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.path.join(HERE, "..", "hardware", "lib", "WOOW.pretty")
MU0 = 4e-7 * math.pi
F0, C_TUN = 13.56e6, 28.5e-12


def segments(name):
    s = open(os.path.join(LIB, name + ".kicad_mod")).read()
    out = []
    for m in re.finditer(r'\(fp_line \(start ([-\d.]+) ([-\d.]+)\) \(end ([-\d.]+) ([-\d.]+)\) \(stroke \(width ([\d.]+)\)[^)]*\)\) \(layer "([FB])\.Cu"\)', s):
        x1, y1, x2, y2, w = map(float, m.groups()[:5])
        out.append((np.array([x1, y1]), np.array([x2, y2]), w, m.group(6)))
    return out


def self_nh(l_mm, w_mm, t_mm=0.035):
    l, wt = l_mm / 10, (w_mm + t_mm) / 10                     # cm
    return 2 * l * (math.log(2 * l / wt) + 0.50049 + wt / (3 * l))


def mutual_nh(a, b, n=60):
    (p1, p2), (q1, q2) = a, b
    d1, d2 = (p2 - p1) / n, (q2 - q1) / n
    if abs(np.dot(d1, d2)) < 1e-12:
        return 0.0
    u = p1 + d1 * (np.arange(n) + 0.5)[:, None]
    v = q1 + d2 * (np.arange(n) + 0.5)[:, None]
    r = np.linalg.norm(u[:, None, :] - v[None, :, :], axis=2)
    return MU0 / (4 * math.pi) * np.dot(d1, d2) * np.sum(1 / r) * 1e6   # mm → nH


def inductance_uh(segs):
    """所有線段（含背面跨線）串成一條導體：同向電流的平行段互感相加、反向相減（dot 自帶正負號）"""
    S = [(a, b) for a, b, w, L in segs if np.linalg.norm(b - a) > 1e-6]
    W = [w for a, b, w, L in segs if np.linalg.norm(b - a) > 1e-6]
    L = sum(self_nh(np.linalg.norm(b - a), w) for (a, b), w in zip(S, W))
    for i in range(len(S)):
        for j in range(i + 1, len(S)):
            L += 2 * mutual_nh(S[i], S[j])
    return L / 1000


def f_res(L_uh, c_ext_pf):
    return 1 / (2 * math.pi * math.sqrt(L_uh * 1e-6 * (C_TUN + c_ext_pf * 1e-12)))


if __name__ == "__main__":
    name = sys.argv[1] if len(sys.argv) > 1 else "NFC_Coil_14.9x14.6mm_8T"
    L = inductance_uh(segments(name))
    c_need = 1 / ((2 * math.pi * F0) ** 2 * L * 1e-6) * 1e12 - C_TUN * 1e12
    print(f"{name}: L = {L:.3f} µH；13.56MHz 需外加 {c_need:.1f} pF")
    for c in (56, 62, 68, 75, 82):
        print(f"  外加 {c:3d} pF → {f_res(L, c) / 1e6:.2f} MHz")
