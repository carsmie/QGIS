import random, urllib.request, sys, os, io, time
from PIL import Image, ImageChops
port, mode, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
random.seed(33); os.makedirs(outdir, exist_ok=True)
U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
     "&WIDTH=1184&HEIGHT=1184&LAYERS=ne_10m_admin_0_countries,ne_10m_lakes,ne_10m_admin_0_boundary_lines_land&BBOX=")
res = [338.5, 84.625, 21.15625, 5.2890625]; n = 200; t = 0; diffs = []
for i in range(n):
    w = 1184 * res[i % 4]; x = random.uniform(-100000, 700000 - w); y = random.uniform(6450000, 7900000 - w)
    t0 = time.time(); d = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); t += time.time() - t0
    f = os.path.join(outdir, f'{i}.png')
    if mode == 'save': open(f, 'wb').write(d)
    else:
        a = Image.open(f).convert('RGBA'); b = Image.open(io.BytesIO(d)).convert('RGBA'); dd = ImageChops.difference(a, b)
        if dd.getbbox(): diffs.append((i, res[i % 4], sum(1 for p in dd.get_flattened_data() if max(p) > 32), max(e[1] for e in dd.getextrema())))
print(f"{mode}: {n} tiles, mean {t/n*1000:.1f} ms" + ('' if mode == 'save' else f", differing tiles: {len(diffs)}; worst (tile, res, pixels>32, maxdiff): {sorted(diffs, key=lambda d: -d[2])[:5]}"))
