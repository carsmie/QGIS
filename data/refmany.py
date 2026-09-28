import random, urllib.request, sys, os, io, time
from PIL import Image, ImageChops
port, mode, outdir, size = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
layers = ','.join(f'group_{g}' for g in range(12))
random.seed(8); os.makedirs(outdir, exist_ok=True)
U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
     f"&WIDTH={size}&HEIGHT={size}&LAYERS={layers}&BBOX=")
res = [338.5, 84.625, 21.15625, 5.2890625]; n = 60; t = 0; diff = 0
for i in range(n):
    w = size * res[i % 4]; x = random.uniform(-100000, 700000 - w); y = random.uniform(6450000, 7900000 - w)
    t0 = time.time(); d = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); t += time.time() - t0
    f = os.path.join(outdir, f'{i}.png')
    if mode == 'save': open(f, 'wb').write(d)
    elif ImageChops.difference(Image.open(f).convert('RGBA'), Image.open(io.BytesIO(d)).convert('RGBA')).getbbox(): diff += 1
print(f"{mode}: {n} tiles {size}px, mean {t/n*1000:.1f} ms" + ('' if mode == 'save' else f", differing: {diff}"))
