import random, time, urllib.request, sys, io
from PIL import Image
random.seed(1); n = int(sys.argv[1]); extra = sys.argv[2] if len(sys.argv) > 2 else ""
U = ("http://127.0.0.1:8811/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=ne_10m_admin_0_countries,ne_10m_lakes,"
     "ne_10m_admin_0_boundary_lines_land&STYLES=&SRS=EPSG:25833&FORMAT=" + (sys.argv[3] if len(sys.argv) > 3 else "image/png") + "&TRANSPARENT=TRUE&WIDTH=1184&HEIGHT=1184" + extra + "&BBOX=")
res = [338.5, 84.625, 21.15625, 5.2890625]
tq = td = size = 0
for i in range(n):
    r = res[i % 4]; w = 1184 * r
    x = random.uniform(-100000, 700000 - w); y = random.uniform(6450000, 7900000 - w)
    t0 = time.time(); data = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); t1 = time.time()
    Image.open(io.BytesIO(data)).load(); t2 = time.time()
    tq += t1 - t0; td += t2 - t1; size += len(data)
print(f"{extra or 'default':20} QGIS {tq/n*1000:5.1f} ms/req | MapProxy decode {td/n*1000:5.1f} ms | {size/n/1e3:7.0f} KB/req")
