# Fetch a fixed set of relief tiles from a server and save or compare them.
import random, urllib.request, sys, os, io, time
from PIL import Image, ImageChops
port, mode, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
layers = sys.argv[4] if len(sys.argv) > 4 else 'relief'
random.seed(21); os.makedirs(outdir, exist_ok=True)
U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
     f"&WIDTH=1184&HEIGHT=1184&LAYERS={layers}&BBOX=")
res = [42.3125, 21.15625, 10.578125, 5.2890625]
diff = 0; t = 0; n = 40
for i in range(n):
    w = 1184 * res[i % 4]; x = random.uniform(222000, 298000 - w); y = random.uniform(6612000, 6688000 - w)  # includes nodata border
    t0 = time.time(); d = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); t += time.time() - t0
    f = os.path.join(outdir, f'{i}.png')
    if mode == 'save':
        open(f, 'wb').write(d)
    else:
        a = Image.open(f).convert('RGBA'); b = Image.open(io.BytesIO(d)).convert('RGBA')
        if ImageChops.difference(a, b).getbbox() is not None: diff += 1
print(f"{mode}: {n} tiles, mean {t/n*1000:.1f} ms" + ('' if mode == 'save' else f", tiles differing from reference: {diff}"))
