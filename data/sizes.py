import random, time, urllib.request, io, sys
from PIL import Image
def run(port, layers, area, res, n, label):
    random.seed(4); t = sz = td = 0
    U = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png"
         f"&TRANSPARENT=TRUE&WIDTH=1184&HEIGHT=1184&LAYERS={layers}&BBOX=")
    for i in range(n):
        w = 1184 * res[i % len(res)]; x = random.uniform(area[0], area[2] - w); y = random.uniform(area[1], area[3] - w)
        t0 = time.time(); d = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); t += time.time() - t0; sz += len(d)
        t1 = time.time(); Image.open(io.BytesIO(d)).load(); td += time.time() - t1
    print(f"{label:8} QGIS {t/n*1000:6.1f} ms/req | MapProxy decode {td/n*1000:5.1f} ms | {sz/n/1e3:6.0f} KB")
run(8812, 'relief,lakes,boundaries', (232000, 6622000, 288000, 6678000), [42.3125, 21.15625, 10.578125, 5.2890625], 80, 'relief')
run(8811, 'ne_10m_admin_0_countries,ne_10m_lakes,ne_10m_admin_0_boundary_lines_land', (-100000, 6450000, 700000, 7900000), [338.5, 84.625, 21.15625, 5.2890625], 200, 'vector')
