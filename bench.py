# Portable QGIS Server benchmark (macOS/Linux/Windows); run relocate.py and fetch_data.py first.
# usage: python bench.py <qgis_mapserver executable> [workload ...]
#   workloads: earth relief many big gfi catmap (default: all)
# Starts qgis_mapserver on the workload's project, warms up, then runs the matching load script.
# Project loading: python projload/load.py <project.qgs> 3 [trust]  (needs a python that imports qgis.core)
import os, socket, subprocess, sys, time

KIT = "@KIT@"
D, X = os.path.join(KIT, "data"), os.path.join(KIT, "xmlbench")
PY = sys.executable
GROUPS = ",".join(f"group_{g}" for g in range(12))
# name: (dir, project, port, load command, warm-up command)
W = {
    "earth": (D, "earth_local.qgs", 8811, [PY, "load.py", "700", "11"], [PY, "load.py", "8", "3"]),
    "relief": (D, "relief.qgs", 8812, [PY, "load3.py", "150", "relief", "5"], [PY, "load3.py", "8", "relief", "9"]),
    "many": (D, "many.qgs", 8840, [PY, "loadmany.py", "250", GROUPS], [PY, "loadmany.py", "20", GROUPS]),
    "big": (D, "big.qgs", 8850, [PY, "loadbig.py", "3000", "l_0_0_0"], [PY, "loadbig.py", "50", "l_0_0_0"]),
    "gfi": (X, "styles_cat.qgs", 8851, [PY, "load.py", "8851", "gfi-json", "150"], [PY, "load.py", "8851", "gfi-json", "10"]),
    "catmap": (X, "styles_cat.qgs", 8852, [PY, "load.py", "8852", "map-default", "120"], [PY, "load.py", "8852", "map-default", "10"]),
}

exe = sys.argv[1]
for name in sys.argv[2:] or list(W):
    d, proj, port, load, warm = W[name]
    env = dict(os.environ, PORT=str(port))
    log = open(os.path.join(KIT, f"srv_{name}.log"), "w")
    srv = subprocess.Popen([exe, "-l", "1", "-p", os.path.join(d, proj), f"127.0.0.1:{port}"], cwd=d, stdout=log, stderr=log)
    try:
        for _ in range(1200):
            try:
                socket.create_connection(("127.0.0.1", port), 0.2).close()
                break
            except OSError:
                time.sleep(0.2)
        subprocess.run(warm, cwd=d, env=env, capture_output=True)
        out = subprocess.run(load, cwd=d, env=env, capture_output=True, text=True).stdout.strip().splitlines()
        print(f"{name:7} {out[-1] if out else '(no output, see srv_' + name + '.log)'}", flush=True)
    finally:
        srv.kill()
        srv.wait()
