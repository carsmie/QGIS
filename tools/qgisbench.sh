#!/bin/zsh
# Compare QGIS Server build-release (Homebrew GDAL) vs build-gdalrect (patched GDAL)
B=@KIT@/data
T=@KIT@/qb
PY=python3
Q=@WORK@/QGIS
mkdir -p $T
ALL=$(python3 -c "print(','.join(f'group_{g}' for g in range(12)))")
N=${N:-250}

cat > $T/tiles.py <<'EOF'
import random, sys, urllib.request, os
port, outdir, layers = sys.argv[1], sys.argv[2], sys.argv[3]
random.seed(99); os.makedirs(outdir, exist_ok=True)
for i in range(40):
    size = 256; w = size * 5.2890625 * random.choice([1, 4, 16])
    x = random.uniform(100000, 600000); y = random.uniform(6500000, 7800000)
    u = (f"http://127.0.0.1:{port}/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833"
         f"&FORMAT=image/png&TRANSPARENT=TRUE&WIDTH={size}&HEIGHT={size}&LAYERS={layers}&BBOX={x},{y},{x+w},{y+w}")
    open(f"{outdir}/{i:02d}.png", "wb").write(urllib.request.urlopen(u).read())
EOF

for build in build-release build-gdalrect; do
  for proj in "many.qgs 8840 loadmany.py" "many_gpkg.qgs 8841 loadmany_g.py"; do
    set -- ${=proj}
    pkill -f "qgis_mapserver.*127.0.0.1:$2" 2>/dev/null; sleep 1
    (cd $B && DYLD_PRINT_LIBRARIES=1 nohup $Q/$build/output/Contents/MacOS/qgis_mapserver -l 1 -p $B/$1 127.0.0.1:$2 > $T/srv_${build}_$2.log 2>&1 &)
    for i in {1..60}; do lsof -nP -iTCP:$2 -sTCP:LISTEN -t >/dev/null 2>&1 && break; sleep 0.5; done
    $PY $T/tiles.py $2 $T/img_${build}_$2 $ALL
    (cd $B && $PY $3 30 $ALL > /dev/null)   # warm-up
    echo "$build $1: $(cd $B && $PY $3 $N $ALL)"
    echo "  libgdal loaded: $(grep -o '[^ ]*libgdal[^ ]*dylib' $T/srv_${build}_$2.log | sort -u | tr '\n' ' ')"
    pkill -f "qgis_mapserver.*127.0.0.1:$2"; sleep 1
  done
done
for p in 8840 8841; do
  echo "tiles $p identical: $(diff -rq $T/img_build-release_$p $T/img_build-gdalrect_$p > /dev/null && echo yes || echo NO) ($(ls $T/img_build-release_$p | wc -l | tr -d ' ') tiles)"
done
