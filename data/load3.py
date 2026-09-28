import random, time, urllib.request, sys
random.seed(int(sys.argv[3]) if len(sys.argv) > 3 else 1); n = int(sys.argv[1]); layers = sys.argv[2]
U = ("http://127.0.0.1:8812/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
     "&WIDTH=1184&HEIGHT=1184&LAYERS=" + layers + "&BBOX=")
res = [42.3125, 21.15625, 10.578125, 5.2890625]  # levels 9..12
ts = []; size = 0
for i in range(n):
    w = 1184 * res[i % 4]; x = random.uniform(232000, 288000 - w); y = random.uniform(6622000, 6678000 - w)
    t0 = time.time(); d = urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); ts.append(time.time() - t0); size += len(d)
print(f"{layers:24} {n} req: mean {sum(ts)/n*1000:6.1f} ms | {size/n/1e3:6.0f} KB/req")
