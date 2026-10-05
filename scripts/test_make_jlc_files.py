"""make_jlc_files.solve_rotation 的單元測試（不需要 KiCad）。

板上焊盤座標取自 V3.0 板（KiCad 絕對座標，Y 向下），JLC 焊盤取自 jlc_footprints.json。
期望角度 = 2026-10-05 在 JLC 下單頁 3D 檢視器逐顆核對過的結果：
正面 U3／Y1 規則表本來就對；背面 Q1、U4、S1 規則表差 90°，D1 是對的（V3.1 換成 B5819W C8598，方向不變）。

  python3 scripts/test_make_jlc_files.py
"""
import json, os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_jlc_files import FOOTPRINTS, solve_rotation  # noqa: E402

JLC = json.load(open(FOOTPRINTS))
# BOM 已經不用、但測試仍要的料號（快取會隨 BOM 刪掉）：焊盤座標直接寫在這裡，測試不依賴目前 BOM
FIXTURE_PADS = {"C7421516": {"1": [[-0.63, 3.59]], "2": [[0.64, 3.59]], "3": [[0.64, -3.59]], "4": [[-0.63, -3.59]]}}
for _code, _pads in FIXTURE_PADS.items():
    JLC.setdefault(_code, {"footprint": "fixture", "pads": _pads})

BOARD = {
    # 位號: (LCSC, 背面?, {焊盤: [(x, y)]}, 期望角度)
    "U3": ("C916347", False, {"1": [(119.675, 121.985)], "2": [(119.675, 120.715)], "3": [(119.675, 119.445)],
                              "4": [(119.675, 118.175)], "5": [(114.725, 118.175)], "6": [(114.725, 119.445)],
                              "7": [(114.725, 120.715)], "8": [(114.725, 121.985)]}, 90),
    "Y1": ("C654957", False, {"1": [(109.6, 124.33)], "2": [(111.8, 124.33)],
                              "3": [(111.8, 122.63)], "4": [(109.6, 122.63)]}, 0),
    "D1": ("C8598", True,  {"1": [(114.0375, 148.295)], "2": [(117.3375, 148.295)]}, 180),
    "Q1": ("C5224182", True, {"1": [(117.05, 146.245)], "2": [(117.05, 144.345)],
                              "3": [(118.925, 145.295)]}, 0),
    "U4": ("C780769", True, {"1": [(135.3625, 133.03)], "2": [(135.3625, 132.08)], "3": [(135.3625, 131.13)],
                             "4": [(137.6375, 131.13)], "5": [(137.6375, 132.08)], "6": [(137.6375, 133.03)]}, 90),
    "S1": ("C7421516", True, {"1": [(112.19, 104.415)], "2": [(112.19, 103.145)],
                              "3": [(119.81, 103.145)], "4": [(119.81, 104.415)]}, 90),
}


class SolveRotation(unittest.TestCase):
    def test_verified_parts(self):
        for ref, (lcsc, bottom, pads, want) in BOARD.items():
            with self.subTest(ref=ref):
                hits, err = solve_rotation(pads, JLC[lcsc]["pads"], bottom)
                self.assertEqual(hits, [want], f"{ref}: 誤差 {err:.2f}mm")

    def test_bottom_is_mirrored_not_just_rotated(self):
        # 同一組焊盤當正面算，SOT-23 的鏡像無法用旋轉得到 → 不可能有解
        lcsc, _, pads, _ = BOARD["Q1"]
        self.assertEqual(solve_rotation(pads, JLC[lcsc]["pads"], False)[0], [])

    def test_numbered_two_pin_part_is_unique(self):
        # 兩腳料只要焊盤有編號，角度就唯一（JLC 依編號擺件）
        board = {"1": [(0.0, 0.0)], "2": [(1.6, 0.0)]}
        self.assertEqual(solve_rotation(board, {"1": [[-0.8, 0.0]], "2": [[0.8, 0.0]]}, False)[0], [0])
        self.assertEqual(solve_rotation(board, {"1": [[0.8, 0.0]], "2": [[-0.8, 0.0]]}, False)[0], [180])

    def test_duplicate_numbers_are_ambiguous(self):
        # 同編號的焊盤點對稱擺放、中心重疊 → 每個角度都對得上，不能當唯一解，要退回規則表
        board = {"1": [(0.0, 0.0), (8.5, 4.5)], "2": [(8.5, 0.0), (0.0, 4.5)]}
        jlc = {"1": [[-4.25, -2.25], [4.25, 2.25]], "2": [[4.25, -2.25], [-4.25, 2.25]]}
        self.assertGreater(len(solve_rotation(board, jlc, False)[0]), 1)

    def test_split_thermal_pad_tolerated(self):
        # 模組底部散熱焊盤在 EasyEDA 拆成多個編號；10 個以上焊盤時容許 10% 對不上
        jlc = {str(n): [[n * 1.0, 0.0]] for n in range(1, 11)}
        board = {str(n): [(10 + n * 1.0, 5.0)] for n in range(1, 11)}
        board["10"] = [(15.0, 9.0)]                     # 一個焊盤位置完全不同
        self.assertEqual(solve_rotation(board, jlc, False)[0], [0])

    def test_too_few_common_pads(self):
        self.assertEqual(solve_rotation({"1": [(0, 0)]}, {"1": [[0, 0]]}, False)[0], [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
