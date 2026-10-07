"""市電網路專用的雙層格點佈線器（0.1mm 格、A*、可打過孔）。

Freerouting 無法表達「市電之間 1.5mm、市電對低壓 6.4mm」兩種間距同時存在，
所以市電輸出線（DO_*）由這支程式繞，其餘低壓網路再交給 Freerouting。
座標用 Gerber 座標（mm，原點=板底邊中點，Y 向上）。
"""
import heapq, math
import numpy as np
import pcbnew
from PIL import Image, ImageDraw

RES = 10.0                       # 每 mm 格數
X0, Y0, W, H = -31.7, 0.0, 63.4, 83.08
OX, OY = 100.0, 100.0
NX, NY = int(W * RES) + 1, int(H * RES) + 1


def set_board(height):
    """板子高度改了（V3.2 上緣加長）就呼叫：格點涵蓋整塊板；板底邊（Gerber 原點）不動"""
    global H, OY, NY
    bottom = OY + H
    H, OY = height, bottom - height
    NY = int(H * RES) + 1


def board_height(board):
    """板框線段端點的 Y 範圍（GetBoardEdgesBoundingBox 會把板邊線寬 0.05 算進去，格點因此偏 0.05mm，
    板邊 0.5mm 的遮罩就少擋一格，走線會貼到 0.48mm）"""
    ys = [pcbnew.ToMM(p.y) for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts
          for p in (d.GetStart(), d.GetEnd())]
    return round(max(ys) - min(ys), 3)


def to_g(v):                     # KiCad VECTOR2I -> gerber mm
    return pcbnew.ToMM(v.x) - OX - W / 2, OY + H - pcbnew.ToMM(v.y)


def to_k(gx, gy):
    return pcbnew.VECTOR2I(pcbnew.FromMM(OX + gx + W / 2), pcbnew.FromMM(OY + H - gy))


def px(gx, gy):
    return (gx - X0) * RES, (Y0 + H - gy) * RES


class Router:
    def __init__(self, board, is_mains, cl_mm=1.5, selv_cl_mm=6.4, width=1.5, edge=0.5, npth_cl=1.5):
        self.b, self.is_mains = board, is_mains
        self.cl, self.selv, self.w, self.edge, self.npth = cl_mm, selv_cl_mm, width, edge, npth_cl
        self.extra = []          # 已繞好的走線：(net, layer, (x1,y1), (x2,y2), width)

    # ---- 障礙物點陣（對某條網路而言）----
    def masks(self, net):
        hw = self.w / 2
        imgs = {L: Image.new("1", (NX, NY), 0) for L in ("F", "B")}
        dr = {L: ImageDraw.Draw(imgs[L]) for L in imgs}

        def infl(other):
            if other == net: return None
            # 沒有網路的銅（連接器固定腳 MP、空腳）也交給 is_mains 判斷：繞市電時（is_mains("") 為 False）照樣
            # 離它 selv；繞低壓時（lambda n: not MAINS(n)）只用小間距。舊寫法一律當市電，V3.4 換 J6（Micro-Lock
            # Plus，固定腳離訊號腳 2.5mm）時把 J6 自己的腳整個圍死，A* 永遠接不上
            c = self.cl if self.is_mains(other or "") else self.selv
            return c + hw

        def circle(L, gx, gy, r):
            a, b = px(gx, gy); rr = r * RES
            dr[L].ellipse([a - rr, b - rr, a + rr, b + rr], fill=1)

        def seg(L, p, q, r):
            a, b = px(*p); c, d = px(*q)
            dr[L].line([a, b, c, d], fill=1, width=max(1, int(2 * r * RES)))
            circle(L, *p, r); circle(L, *q, r)

        for fp in self.b.GetFootprints():
            for pad in fp.Pads():
                pn = pad.GetNetname()
                gx, gy = to_g(pad.GetPosition())
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    r = pcbnew.ToMM(pad.GetDrillSize().x) / 2 + self.npth + hw
                    circle("F", gx, gy, r); circle("B", gx, gy, r); continue
                d = infl(pn)
                if d is None: continue
                bb = pad.GetBoundingBox()
                x1, y1 = to_g(bb.GetOrigin()); x2, y2 = to_g(bb.GetEnd())
                layers = ["F", "B"] if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else (["B"] if pad.IsOnLayer(pcbnew.B_Cu) else ["F"])
                for L in layers:
                    a, b_ = px(min(x1, x2) - d, max(y1, y2) + d); c, e = px(max(x1, x2) + d, min(y1, y2) - d)
                    dr[L].rounded_rectangle([a, b_, c, e], radius=d * RES, fill=1)
        for t in self.b.GetTracks():
            d = infl(t.GetNetname())
            if d is None: continue
            if t.GetClass() == "PCB_VIA":
                gx, gy = to_g(t.GetPosition()); r = pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu)) / 2 + d
                circle("F", gx, gy, r); circle("B", gx, gy, r)
            else:
                L = "F" if t.GetLayer() == pcbnew.F_Cu else "B"
                seg(L, to_g(t.GetStart()), to_g(t.GetEnd()), pcbnew.ToMM(t.GetWidth()) / 2 + d)
        for (n, L, p, q, w) in self.extra:
            d = infl(n)
            if d is not None: seg(L, p, q, w / 2 + d)
        zones = list(self.b.Zones())
        for fp in self.b.GetFootprints():                # 封裝內建的禁布區（ESP32 天線下方）不在 board.Zones() 裡
            zones += list(fp.Zones())
        for z in zones:                                  # 禁布區（ESP32 天線、NFC 線圈）
            # NFC 線圈的禁布區允許走線（給自己的背面跨線用），但禁鋪銅：別的網路照樣不准進去
            if z.GetIsRuleArea() and (z.GetDoNotAllowTracks() or z.GetDoNotAllowZoneFills()):
                pts = [px(*to_g(z.Outline().CVertex(i))) for i in range(z.Outline().TotalVertices())]
                for L in ("F", "B"):
                    dr[L].polygon(pts, fill=1)
                    # 往外擴走線半寬＋0.05：只塗多邊形的話，走線中心貼邊、線寬會吃進禁布區
                    dr[L].line(pts + pts[:1], fill=1, width=max(1, int(round(2 * (hw + 0.05) * RES))))
        m = {L: np.array(imgs[L], dtype=bool) for L in imgs}
        e = int(round((self.edge + hw) * RES))
        for L in m:
            m[L][:e, :] = m[L][-e:, :] = True; m[L][:, :e] = m[L][:, -e:] = True
        # 非矩形板框（V3.2 起是 L 形）：板外一律是障礙，再往內擴「板邊間距＋半線寬」
        out = self._outside(e)
        if out is not None:
            for L in m:
                m[L] |= out
        return m

    def _outside(self, e):
        if getattr(self, "_outside_cache", None) is not None:
            return self._outside_cache
        from PIL import ImageFilter
        ps = pcbnew.SHAPE_POLY_SET()
        if not self.b.GetBoardPolygonOutlines(ps, False) or ps.OutlineCount() == 0:
            return None
        ol = ps.Outline(0)
        pts = [px(*to_g(ol.CPoint(i))) for i in range(ol.PointCount())]
        img = Image.new("L", (NX, NY), 255); ImageDraw.Draw(img).polygon(pts, fill=0)     # 板內 0、板外 255
        img = img.filter(ImageFilter.MaxFilter(2 * e + 1))
        self._outside_cache = np.array(img) > 0
        return self._outside_cache

    def terminals(self, net):
        out = []
        for fp in self.b.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() == net:
                    L = ["F", "B"] if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else (["B"] if pad.IsOnLayer(pcbnew.B_Cu) else ["F"])
                    out.append((to_g(pad.GetPosition()), L))
        return out

    def route_net(self, net, via_cost=40, clear_mm=1.2, stamp_r=1.0, keep_cl=None):
        """clear_mm：終點焊盤周圍清掉障礙的半徑；None = 只清自己焊盤本身（細腳距 IC 用，避免壓到鄰腳）
        stamp_r：焊盤周圍多大範圍算「已接上」（路徑可從這裡出發）。1.0 是舊行為；細腳焊盤（SOP 0.6mm 寬）要用 0，
        否則走線會停在焊盤外 1mm、而且這圈不檢查障礙，可能壓到別的網路
        keep_cl：clear_mm=None 清掉自己焊盤（含沿長邊延伸的出腳通道）時，別的網路的銅仍以這個間距算障礙。
        None 是舊行為（整塊清掉）—— 這會讓過孔打在旁邊別條網路的走線上（V3.2 的 DI_2 過孔壓到 DI_3）"""
        m = self.masks(net)
        terms = self.terminals(net)
        if len(terms) < 2: return []
        tree = set()                                   # (layer, ix, iy)

        def cell(g): a, b = px(*g); return int(round(a)), int(round(b))

        def stamp(g, Ls, r=stamp_r):
            ix, iy = cell(g); rr = int(r * RES)
            for L in Ls:
                for dx in range(-rr, rr + 1):
                    for dy in range(-rr, rr + 1):
                        if dx * dx + dy * dy <= rr * rr: tree.add((L, ix + dx, iy + dy))
        stamp(*terms[0])
        if clear_mm is None:                           # 只清自己焊盤（不含鄰腳的間距膨脹）
            for fp in self.b.GetFootprints():
                for pad in fp.Pads():
                    if pad.GetNetname() != net: continue
                    bb = pad.GetBoundingBox()
                    x1, y1 = to_g(bb.GetOrigin()); x2, y2 = to_g(bb.GetEnd())
                    xa, xb, ya, yb = min(x1, x2), max(x1, x2), min(y1, y2), max(y1, y2)
                    ext = 0.35                             # 沿焊盤長邊往外延伸：細腳距 IC 的出腳通道
                    if xb - xa >= yb - ya: xa, xb = xa - ext, xb + ext
                    else: ya, yb = ya - ext, yb + ext
                    a, b_ = px(xa, yb); c, e = px(xb, ya)
                    Ls = ["F", "B"] if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else (["B"] if pad.IsOnLayer(pcbnew.B_Cu) else ["F"])
                    for L in Ls:
                        m[L][int(b_):int(e) + 1, int(a):int(c) + 1] = False
            if keep_cl is not None:                    # 別的網路的實際銅（小間距）還是要擋
                saved = self.cl, self.selv
                self.cl, self.selv = keep_cl, max(keep_cl, self.selv)
                hard = self.masks(net)
                self.cl, self.selv = saved
                for L in ("F", "B"):
                    m[L] |= hard[L]
        else:                                          # 自己焊盤附近不算障礙
            r = int(round(clear_mm * RES))
            for L in ("F", "B"):
                for g, Ls in terms:
                    ix, iy = cell(g)
                    m[L][max(0, iy - r):iy + r + 1, max(0, ix - r):ix + r + 1] = False
        segs = []
        for g, Ls in terms[1:]:
            goal = set(); ix, iy = cell(g)
            for L in Ls: goal.add((L, ix, iy))
            path = self.astar(m, tree, goal, via_cost, self.via_block(m))
            if path is None: raise RuntimeError(f"{net}: 找不到路徑（到 {g}）")
            for p in path: tree.add(p)
            stamp(g, Ls)
            segs += self.to_segments(path)
        return segs

    def via_block(self, m, via_d=0.6):
        """過孔比走線粗：把障礙再往外擴 (過孔半徑 - 走線半寬)，只用來判斷能不能打過孔"""
        from PIL import ImageFilter
        k = max(0, int(math.ceil((via_d / 2 - self.w / 2) * RES)))
        if k == 0: return m["F"] | m["B"]
        both = Image.fromarray(((m["F"] | m["B"]) * 255).astype(np.uint8))
        return np.array(both.filter(ImageFilter.MaxFilter(2 * k + 1))) > 0

    def astar(self, m, starts, goal, via_cost, vblock=None):
        gl = list(goal)
        def h(n): return min(math.hypot(n[1] - q[1], n[2] - q[2]) for q in gl)
        openq = []; best = {}; prev = {}
        for s in starts:
            if 0 <= s[1] < NX and 0 <= s[2] < NY:
                best[s] = 0; heapq.heappush(openq, (h(s), 0, s))
        dirs = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1), (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
        while openq:
            f, gcost, n = heapq.heappop(openq)
            if n in goal:
                path = [n]
                while path[-1] in prev: path.append(prev[path[-1]])
                return path[::-1]
            if gcost > best.get(n, 1e18): continue
            L, x, y = n
            nb = [((L, x + dx, y + dy), c) for dx, dy, c in dirs]
            nb.append((("B" if L == "F" else "F", x, y), via_cost))
            for q, c in nb:
                QL, qx, qy = q
                if not (0 <= qx < NX and 0 <= qy < NY): continue
                if m[QL][qy, qx] and q not in goal: continue
                if c == via_cost and (vblock[qy, qx] if vblock is not None else (m["F"][qy, qx] or m["B"][qy, qx])): continue
                ng = gcost + c
                if ng < best.get(q, 1e18):
                    best[q] = ng; prev[q] = n; heapq.heappush(openq, (ng + h(q), ng, q))
        return None

    def to_segments(self, path):
        def g(ix, iy): return ix / RES + X0, Y0 + H - iy / RES
        segs, start = [], path[0]
        for i in range(1, len(path)):
            a, b = path[i - 1], path[i]
            if a[0] != b[0]:                                       # 換層
                if start != a: segs.append(("T", a[0], g(*start[1:]), g(*a[1:])))
                segs.append(("V", g(*a[1:]))); start = b; continue
            nxt = path[i + 1] if i + 1 < len(path) else None
            if nxt is None or nxt[0] != b[0] or (nxt[1] - b[1], nxt[2] - b[2]) != (b[1] - a[1], b[2] - a[2]):
                segs.append(("T", b[0], g(*start[1:]), g(*b[1:]))); start = b
        return segs

    def commit(self, net, segs, netinfo):
        for s in segs:
            if s[0] == "T":
                _, L, p, q = s
                if p == q: continue
                t = pcbnew.PCB_TRACK(self.b); t.SetStart(to_k(*p)); t.SetEnd(to_k(*q))
                t.SetWidth(pcbnew.FromMM(self.w)); t.SetLayer(pcbnew.F_Cu if L == "F" else pcbnew.B_Cu)
                t.SetNet(netinfo); t.SetLocked(True); self.b.Add(t)
                self.extra.append((net, L, p, q, self.w))
            else:
                v = pcbnew.PCB_VIA(self.b); v.SetPosition(to_k(*s[1])); v.SetDrill(pcbnew.FromMM(0.6))
                v.SetWidth(pcbnew.FromMM(1.2)); v.SetNet(netinfo); v.SetLocked(True); self.b.Add(v)
                self.extra.append((net, "F", s[1], s[1], 1.2)); self.extra.append((net, "B", s[1], s[1], 1.2))
