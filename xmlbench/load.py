# usage: load.py <port> <mode> <n> [save_dir]
#   mode: map-default | map-alt | gfi-xml | gfi-json | gfi-html | gfi-text
import random, sys, time, urllib.request, os

port, mode, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
save = sys.argv[4] if len(sys.argv) > 4 else None
layers = [f"layer_{g}_{i}" for g in range(12) for i in range(10)]
L = ",".join(layers)
base = f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.3.0&CRS=EPSG:25833&"
fmt = {"gfi-xml": "text/xml", "gfi-json": "application/json", "gfi-html": "text/html", "gfi-text": "text/plain"}
random.seed(5)
ts = []
nbytes = 0
for i in range(n):
    size = 256
    w = size * 5.2890625 * random.choice([1, 4, 16])
    x = random.uniform(100000, 600000); y = random.uniform(6500000, 7800000)
    bbox = f"{x},{y},{x + w},{y + w}"   # EPSG:25833 axis order is easting,northing
    if mode.startswith("map"):
        styles = "" if mode == "map-default" else ",".join(["alt"] * len(layers))
        u = base + (f"REQUEST=GetMap&FORMAT=image/png&TRANSPARENT=TRUE&WIDTH={size}&HEIGHT={size}"
                    f"&LAYERS={L}&STYLES={styles}&BBOX={bbox}")
    else:
        u = base + (f"REQUEST=GetFeatureInfo&WIDTH={size}&HEIGHT={size}&LAYERS={L}&QUERY_LAYERS={L}&STYLES="
                    f"&INFO_FORMAT={fmt[mode]}&FEATURE_COUNT=10&I=128&J=128&BBOX={bbox}")
    t0 = time.perf_counter()
    body = urllib.request.urlopen(u).read()
    ts.append(time.perf_counter() - t0)
    nbytes += len(body)
    if save:
        os.makedirs(save, exist_ok=True)
        open(f"{save}/{i:03d}", "wb").write(body)
ts.sort()
print(f"{mode:12} n={n}: mean {sum(ts) / n * 1000:7.2f} ms  median {ts[n // 2] * 1000:7.2f} ms  "
      f"avg response {nbytes / n / 1024:.1f} KB")
