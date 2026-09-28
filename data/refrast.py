import random, urllib.request, sys, os, io, time
from PIL import Image, ImageChops
port, mode, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(outdir, exist_ok=True); diff = 0; n = 0; t = 0
for layer in ('dem_byte', 'dem_u16', 'dem_f64', 'dem'):
    random.seed(5)
    for i in range(12):
        res = [100, 42.3125, 10.578125, 3.1][i % 4]; w = 1184 * res
        x = random.uniform(222000, 298000 - w); y = random.uniform(6612000, 6688000 - w)
        U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
             f"&WIDTH=1184&HEIGHT=1184&LAYERS={layer}&BBOX={x},{y},{x+w},{y+w}")
        t0 = time.time(); d = urllib.request.urlopen(U).read(); t += time.time() - t0; n += 1
        f = os.path.join(outdir, f'{layer}_{i}.png')
        if mode == 'save': open(f, 'wb').write(d)
        elif ImageChops.difference(Image.open(f).convert('RGBA'), Image.open(io.BytesIO(d)).convert('RGBA')).getbbox(): diff += 1; print('DIFF', layer, i)
print(f"{mode}: {n} tiles, mean {t/n*1000:.1f} ms" + ('' if mode == 'save' else f", differing: {diff}"))
