import random, time, urllib.request, io, sys
from PIL import Image, ImageChops
def run(ports, layers, area, res, n, label):
    random.seed(4)
    reqs = []
    for i in range(n):
        w = 1184 * res[i % len(res)]; x = random.uniform(area[0], area[2] - w); y = random.uniform(area[1], area[3] - w)
        reqs.append(f"{x},{y},{x+w},{y+w}")
    base = None; stats = {}
    for port, name in ports:
        U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png"
             f"&TRANSPARENT=TRUE&WIDTH=1184&HEIGHT=1184&LAYERS={layers}&BBOX=")
        urllib.request.urlopen(U + reqs[0]).read()  # warm up
        t = sz = td = 0; imgs = []
        for b in reqs:
            t0 = time.time(); d = urllib.request.urlopen(U + b).read(); t += time.time() - t0; sz += len(d)
            t1 = time.time(); im = Image.open(io.BytesIO(d)); im.load(); td += time.time() - t1; imgs.append(im)
        same = '' if base is None else ('pixel-identical' if all(ImageChops.difference(a.convert('RGBA'), b.convert('RGBA')).getbbox() is None and a.info.get('dpi') == b.info.get('dpi') for a, b in zip(base, imgs)) else 'DIFFERENT')
        base = base or imgs
        print(f"{label:8} {name:22} QGIS {t/n*1000:6.1f} ms/req | MapProxy decode {td/n*1000:5.1f} ms | {sz/n/1e3:6.0f} KB | {same}", flush=True)
run([(8820, 'Qt (today)'), (8821, 'fast, level 1'), (8822, 'fast, level 6')], 'relief,lakes,boundaries',
    (232000, 6622000, 288000, 6678000), [42.3125, 21.15625, 10.578125, 5.2890625], 80, 'relief')
run([(8830, 'Qt (today)'), (8831, 'fast, level 1'), (8832, 'fast, level 6')],
    'ne_10m_admin_0_countries,ne_10m_lakes,ne_10m_admin_0_boundary_lines_land',
    (-100000, 6450000, 700000, 7900000), [338.5, 84.625, 21.15625, 5.2890625], 200, 'vector')
