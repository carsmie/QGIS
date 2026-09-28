#!/bin/zsh
# Time XML-heavy request types on the patched-GDAL QGIS Server build.
X=@KIT@/xmlbench
Q=@WORK@/QGIS/build-gdalrect/output/Contents/MacOS/qgis_mapserver
PY=python3
N=${N:-100}
cd $X
for proj in styles_simple styles_cat; do
  $Q -l 1 -p $X/$proj.qgs 127.0.0.1:8850 > $X/srv_$proj.log 2>&1 &
  SPID=$!
  for i in {1..240}; do lsof -nP -iTCP:8850 -sTCP:LISTEN -t >/dev/null 2>&1 && break; sleep 0.5; done
  echo "== $proj"
  for mode in map-default map-alt gfi-xml gfi-json gfi-html gfi-text; do
    $PY load.py 8850 $mode 10 > /dev/null          # warm-up
    $PY load.py 8850 $mode $N $X/out_${proj}_$mode
  done
  echo "  map-alt tiles identical to map-default: $(diff -rq $X/out_${proj}_map-default $X/out_${proj}_map-alt > /dev/null && echo yes || echo NO)"
  kill $SPID; wait $SPID 2>/dev/null
done
