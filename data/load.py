import random, time, urllib.request, sys
PORT = __import__("os").environ.get("PORT", "8811")
random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 1)
n = int(sys.argv[1])
EXTRA = sys.argv[3] if len(sys.argv) > 3 else ""
U = ("http://127.0.0.1:" + PORT + "/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=ne_10m_admin_0_countries,ne_10m_lakes,"
     "ne_10m_admin_0_boundary_lines_land&STYLES=&SRS=EPSG:25833&FORMAT=image/png&TRANSPARENT=TRUE&WIDTH=1184&HEIGHT=1184&BBOX=")
res = [338.5, 84.625, 21.15625, 5.2890625]   # levels 6, 8, 10, 12 of the utm33 grid
ts = []
for i in range(n):
    r = res[i % len(res)]; w = 1184 * r
    x = random.uniform(-100000, 700000 - w); y = random.uniform(6450000, 7900000 - w)
    t0 = time.time(); urllib.request.urlopen(U + f"{x},{y},{x+w},{y+w}" + EXTRA).read(); ts.append(time.time() - t0)
ts.sort()
print(f"{n} req: mean {sum(ts)/n*1000:.1f} ms  median {ts[n//2]*1000:.1f} ms  p90 {ts[int(n*.9)]*1000:.1f} ms")
