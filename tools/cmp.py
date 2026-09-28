# Byte-compare responses of two qgis_mapserver builds on the benchmark projects.
# usage: cmp.py <base_mapserver> <new_mapserver>
import os, random, subprocess, sys, time, socket, urllib.request

S = "@KIT@/data"
X = "@KIT@/xmlbench"
BASE, NEW = sys.argv[1], sys.argv[2]
LOG = os.path.dirname(os.path.abspath(__file__))

def bboxes(n, seed, res, xr, yr, size):
    r = random.Random(seed)
    for i in range(n):
        w = size * res[i % len(res)]
        x = r.uniform(xr[0], xr[1] - w); y = r.uniform(yr[0], yr[1] - w)
        yield f"{x},{y},{x + w},{y + w}"

def getmap11(layers, size, styles=""):
    return (f"SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES={styles}&SRS=EPSG:25833&FORMAT=image/png"
            f"&TRANSPARENT=TRUE&WIDTH={size}&HEIGHT={size}&LAYERS={layers}&BBOX=")

grp = ",".join(f"group_{g}" for g in range(12))
cat = ",".join(f"layer_{g}_{i}" for g in range(12) for i in range(10))
cat13 = "SERVICE=WMS&VERSION=1.3.0&CRS=EPSG:25833&"
cases = {
    "earth": (S, "earth_local.qgs", [getmap11("ne_10m_admin_0_countries,ne_10m_lakes,ne_10m_admin_0_boundary_lines_land", 1184) + b
                                      for b in bboxes(24, 11, [338.5, 84.625, 21.15625, 5.2890625], (-100000, 700000), (6450000, 7900000), 1184)]),
    "relief": (S, "relief.qgs", [getmap11("relief", 1184) + b
                                  for b in bboxes(8, 5, [42.3125, 21.15625, 10.578125, 5.2890625], (232000, 288000), (6622000, 6678000), 1184)]),
    "many": (S, "many.qgs", [getmap11(grp, 256) + b
                              for b in bboxes(40, 3, [5.2890625 * k for k in (1, 4, 16)], (100000, 600000 + 90000), (6500000, 7800000 + 90000), 256)]),
    "big": (S, "big.qgs", [getmap11("l_0_0_0", 256) + b
                            for b in bboxes(90, 3, [5.2890625 * k for k in (1, 16, 64)], (100000, 700000), (6500000, 7900000), 256)]),
    "cat": (X, "styles_cat.qgs",
            [cat13 + f"REQUEST=GetMap&FORMAT=image/png&TRANSPARENT=TRUE&WIDTH=256&HEIGHT=256&LAYERS={cat}&STYLES={st}&BBOX={b}"
             for st in ("", ",".join(["alt"] * 120), "")
             for b in bboxes(10, 5, [5.2890625 * k for k in (1, 4, 16)], (100000, 690000), (6500000, 7890000), 256)]
            + [cat13 + f"REQUEST=GetFeatureInfo&WIDTH=256&HEIGHT=256&LAYERS={cat}&QUERY_LAYERS={cat}&STYLES="
               f"&INFO_FORMAT={fmt}&FEATURE_COUNT=10&I=128&J=128&BBOX={b}"
               for fmt in ("application/json", "text/xml")
               for b in bboxes(10, 7, [5.2890625 * k for k in (1, 4, 16)], (100000, 690000), (6500000, 7890000), 256)]),
}

def start(exe, d, proj, port, tag):
    log = open(f"{LOG}/cmp_{tag}.log", "w")
    p = subprocess.Popen([exe, "-l", "1", "-p", f"{d}/{proj}", f"127.0.0.1:{port}"], cwd=d, stdout=log, stderr=log)
    for _ in range(600):
        try:
            socket.create_connection(("127.0.0.1", port), 0.2).close(); return p
        except OSError:
            time.sleep(0.2)
    raise RuntimeError(f"{tag} did not start")

only = sys.argv[3:] or list(cases)
total_diff = 0
for name in only:
    d, proj, reqs = cases[name]
    pb = start(BASE, d, proj, 8901, f"{name}_base"); pn = start(NEW, d, proj, 8902, f"{name}_new")
    try:
        diff = []
        for i, q in enumerate(reqs):
            a = urllib.request.urlopen(f"http://127.0.0.1:8901/?{q}").read()
            b = urllib.request.urlopen(f"http://127.0.0.1:8902/?{q}").read()
            if a != b:
                diff.append(i)
                open(f"{LOG}/diff_{name}_{i}_base", "wb").write(a); open(f"{LOG}/diff_{name}_{i}_new", "wb").write(b)
        print(f"{name:7} {len(reqs):3d} requests, {len(diff)} differ {diff[:10]}", flush=True)
        total_diff += len(diff)
    finally:
        pb.kill(); pn.kill(); pb.wait(); pn.wait()
sys.exit(1 if total_diff else 0)
