import random, time, urllib.request, sys
random.seed(3); n = int(sys.argv[1]); layers = sys.argv[2]; size = int(sys.argv[3]) if len(sys.argv) > 3 else 256
U = (f"http://127.0.0.1:8840/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE"
     f"&WIDTH={size}&HEIGHT={size}&LAYERS={layers}&BBOX=")
ts = []
for i in range(n):
    w = size * 5.2890625; x = random.uniform(100000, 600000); y = random.uniform(6500000, 7800000)
    t0 = time.time(); urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}").read(); ts.append(time.time() - t0)
ts.sort(); print(f"{layers[:30]:30} {size}px: mean {sum(ts)/n*1000:.2f} ms median {ts[n//2]*1000:.2f} ms")
