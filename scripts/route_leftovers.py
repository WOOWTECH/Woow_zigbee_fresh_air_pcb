"""補繞 Freerouting 沒繞通的低壓網路（用 mains_router 的格點 A*：對低壓 0.15mm、線寬 0.2mm、對市電 6.5mm、可換層）。
用法：python3 route_leftovers.py /Input_1 [...]
"""
import os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import mains_router
PCB = os.path.abspath(os.path.join(HERE, "..", "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb"))
MAINS = lambda n: n.startswith("/AC_") or n.startswith("/DO_") or n.startswith("unconnected-(K")
b = pcbnew.LoadBoard(PCB)
mains_router.set_board(mains_router.board_height(b))
# Router 的 is_mains 參數意思是「與我同一類（用小間距）」：這裡同類 = 低壓
R = mains_router.Router(b, lambda n: not MAINS(n), cl_mm=0.15, selv_cl_mm=6.5, width=0.2, edge=0.5, npth_cl=0.5)
for net in sys.argv[1:]:
    segs = R.route_net(net, via_cost=15, clear_mm=None)
    R.commit(net, segs, b.FindNet(net))
    for t in b.GetTracks():                                   # 小走線不鎖定、過孔用一般尺寸
        if t.GetNetname() == net and t.GetClass() == "PCB_VIA":
            t.SetDrill(pcbnew.FromMM(0.3)); t.SetWidth(pcbnew.FromMM(0.6))
    print(net, sum(1 for s in segs if s[0] == "T"), "段", sum(1 for s in segs if s[0] == "V"), "過孔")
pcbnew.SaveBoard(PCB, b)
