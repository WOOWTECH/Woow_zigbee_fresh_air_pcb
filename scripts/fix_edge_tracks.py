"""刪掉離板邊不到 0.5mm 的低壓走線（Freerouting 的 DSN 不含 .kicad_dru 的 edge 規則），之後由鋪銅或 route_leftovers.py 補回。
用法：python3 fix_edge_tracks.py   → 印出被刪走線所屬網路"""
import json, os, subprocess, tempfile
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.abspath(os.path.join(HERE, "..", "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb"))
out = tempfile.mktemp(suffix=".json")
subprocess.run(["kicad-cli", "pcb", "drc", "--format", "json", "-o", out, PCB], capture_output=True)
bad = {i["uuid"] for v in json.load(open(out))["violations"] if v["type"] == "copper_edge_clearance"
       for i in v["items"] if i["description"].startswith(("Track", "Via"))}
b = pcbnew.LoadBoard(PCB)
victims = [t for t in b.GetTracks() if t.m_Uuid.AsString() in bad and not t.IsLocked()]
nets = sorted({t.GetNetname() for t in victims})
for t in victims:
    b.Remove(t)
pcbnew.SaveBoard(PCB, b)
print(" ".join(nets))
