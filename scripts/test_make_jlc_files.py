"""make_jlc_files.solve_rotation 的單元測試（不需要 KiCad）。

板上焊盤座標取自 V3.0 板（KiCad 絕對座標，Y 向下），JLC 焊盤取自 jlc_footprints.json。
期望角度 = 2026-10-05 在 JLC 下單頁 3D 檢視器逐顆核對過的結果：
正面 U3／Y1 規則表本來就對；背面 Q1、U4、S1 規則表差 90°，D1 是對的（V3.1 換成 B5819W C8598，方向不變）。

  python3 scripts/test_make_jlc_files.py
"""
import json, os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_jlc_files import FOOTPRINTS, solve_rotation, solve_rotation_by_body  # noqa: E402

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


class SolveRotationByBody(unittest.TestCase):
    """插拔式端子這類「焊盤一排、等距」的料：焊盤怎麼轉都對得上，編號只是標籤，真正決定能不能用的是開口朝哪邊。
    2026-10-07 發現：V3.4 的 J5 依編號算出 0°，但 JLC 封裝的本體在 +Y、板上要朝 −Y（板邊），0° 會讓開口朝板內。
    本體方向：板上用 courtyard 外框、JLC 用絲印外框，各自相對焊盤中心的偏移。"""

    ROW7 = {str(n): [[-7.62 + 2.54 * (n - 1), 0.0]] for n in range(1, 8)}     # JLC KF2EDGR-2.54-7P：Pin1 在 −X
    JLC_BODY = (-9.9, 9.9, -1.3, 9.1)                                          # 絲印：本體往 +Y

    def board(self, x0, y0, step, body):
        pads = {str(n): [(x0 + step * (n - 1), y0)] for n in range(1, 8)}
        return pads, body

    def test_j5_body_toward_board_edge_needs_180(self):
        # 板上 J5：Pin1 在 −X（102.48）、本體往 −Y（開口朝上緣 y=100）
        pads, body = self.board(102.48, 106.9, 2.54, (100.31, 119.89, 100.0, 108.1))
        self.assertEqual(solve_rotation_by_body(pads, body, self.ROW7, self.JLC_BODY, False), [180])

    def test_reversed_numbering_same_body_direction_is_0(self):
        # P3 的情況：板上 Pin1 在 +X（編號反向）、本體同樣往 +Y → 0°（編號照規則會算 180°，開口會朝板內）
        pads, body = self.board(117.78, 175.0, -2.54, (98.0, 122.0, 172.6, 183.0))
        self.assertEqual(solve_rotation_by_body(pads, body, self.ROW7, self.JLC_BODY, False), [0])

    def test_not_applicable_without_body_offset(self):
        # 本體置中（電阻、保險絲）：不能用本體判斷 → 空清單，交回編號比對
        pads = {"1": [(0.0, 0.0)], "2": [(5.08, 0.0)]}
        self.assertEqual(solve_rotation_by_body(pads, (-2.0, 7.0, -4.2, 4.2), {"1": [[-2.54, 0]], "2": [[2.54, 0]]},
                                                (-4.2, 4.3, -4.2, 4.2), False), [])

    def test_not_applicable_when_pad_pattern_is_asymmetric(self):
        # SOT-23 這類焊盤不對稱的料：編號比對本來就唯一，不歸這裡管
        pads = {"1": [(0.0, 0.0)], "2": [(0.0, 1.9)], "3": [(1.9, 0.95)]}
        jlc = {"1": [[-0.95, 0.95]], "2": [[0.95, 0.95]], "3": [[0.0, -0.95]]}
        self.assertEqual(solve_rotation_by_body(pads, (-1.0, 3.0, -1.0, 3.0), jlc, (-1.5, 1.5, -1.5, 1.5), False), [])

    def test_vertical_body_offset_on_rotated_footprint(self):
        # 板上整排轉 90°（直排、本體往 +X），JLC 本體往 +Y → 270°（KiCad 角度定義，見 _rot）
        pads = {str(n): [(10.0, 20.0 + 2.54 * (n - 1))] for n in range(1, 8)}
        body = (8.7, 19.1, 17.72, 37.52)                 # 腳後 1.3、前 9.1；Y 跟焊盤排同中心
        hits = solve_rotation_by_body(pads, body, self.ROW7, self.JLC_BODY, False)
        self.assertEqual(len(hits), 1)
        from make_jlc_files import _rot
        v = _rot((0.0, 1.0), hits[0])                  # JLC 本體方向（+Y）轉過去之後要指向 +X
        self.assertGreater(v[0], 0.9)


if __name__ == "__main__":
    unittest.main(verbosity=2)
