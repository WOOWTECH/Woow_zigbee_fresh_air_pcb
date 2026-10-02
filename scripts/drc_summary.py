"""跑 KiCad DRC 並整理成摘要（錯誤分類、安規規則違規數）。用法：python3 drc_summary.py [board.kicad_pcb]"""
import collections, json, os, re, subprocess, sys, tempfile
args = [a for a in sys.argv[1:] if not a.startswith("-")]
pcb = os.path.abspath(args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hardware", "WO30109_FreshAir", "WO30109_FreshAir.kicad_pcb"))
out = tempfile.mktemp(suffix=".json")
subprocess.run(["kicad-cli", "pcb", "drc", "--schematic-parity", "--format", "json", "-o", out, pcb], capture_output=True)
d = json.load(open(out))
def g(p): return (round(p["x"] - 131.7, 1), round(183.08 - p["y"], 1))
if "--unconnected-nets" in sys.argv:                       # 給 build_v20.sh：列出沒連上的網路名
    nets = []
    for v in d["unconnected_items"]:
        m = re.search(r"\[(/[^\]]+)\]", v["items"][0]["description"])
        if m and m.group(1) not in nets: nets.append(m.group(1))
    print(" ".join(nets)); sys.exit(0)
err = [v for v in d["violations"] if v["severity"] == "error"]
print("errors:", dict(collections.Counter(v["type"] for v in err)))
print("warnings:", dict(collections.Counter(v["type"] for v in d["violations"] if v["severity"] != "error")))
print("unconnected:", len(d["unconnected_items"]), " parity:", len(d["schematic_parity"]))
rules = collections.Counter((re.search(r"\((?:netclass|rule) '([^']+)'", v["description"]) or [None, v["type"]])[1] for v in err if "clearance" in v["type"])
print("clearance by rule:", dict(rules))
if "-v" in sys.argv:
    for v in err + d["unconnected_items"] + d["schematic_parity"]:
        print(" ", v["type"], "|", " | ".join(i["description"][:48] + str(g(i["pos"])) for i in v["items"]))
