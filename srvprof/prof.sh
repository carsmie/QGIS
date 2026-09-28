#!/bin/zsh
# Profile QGIS Server workloads (build-gdalrect by default).
# usage: prof.sh <workload>...   workloads: earth relief many big gfi
S=@KIT@/data
X=@KIT@/xmlbench
T=@KIT@/srvprof
PY=python3
BUILD=${BUILD:-build-gdalrect}
Q=@WORK@/QGIS/$BUILD/output/Contents/MacOS/qgis_mapserver
ALL=$(python3 -c "print(','.join(f'group_{g}' for g in range(12)))")
for w in "$@"; do
  case $w in
    earth)  dir=$S; proj=earth_local.qgs; port=8811; load=(env PORT=8811 $PY load.py 700 11); warm=(env PORT=8811 $PY load.py 8 3) ;;
    relief) dir=$S; proj=relief.qgs;      port=8812; load=($PY load3.py 150 relief 5); warm=($PY load3.py 8 relief 9) ;;
    many)   dir=$S; proj=many.qgs;        port=8840; load=($PY loadmany.py 250 $ALL); warm=($PY loadmany.py 20 $ALL) ;;
    big)    dir=$S; proj=big.qgs;         port=8850; load=($PY loadbig.py 3000 l_0_0_0); warm=($PY loadbig.py 50 l_0_0_0) ;;
    gfi)    dir=$X; proj=styles_cat.qgs;  port=8851; load=($PY load.py 8851 gfi-json 150); warm=($PY load.py 8851 gfi-json 10) ;;
    catmap) dir=$X; proj=styles_cat.qgs;  port=8852; load=($PY load.py 8852 map-default 120); warm=($PY load.py 8852 map-default 10) ;;
  esac
  cd $dir
  $Q -l 1 -p $dir/$proj 127.0.0.1:$port > $T/srv_$w.log 2>&1 &
  SPID=$!
  for i in {1..240}; do lsof -nP -iTCP:$port -sTCP:LISTEN -t >/dev/null 2>&1 && break; sleep 0.5; done
  $warm > /dev/null 2>&1
  $load > $T/load_$w.txt 2>&1 &
  LPID=$!
  sleep 1
  sample $SPID 12 -mayDie -file $T/sample_$w.txt > /dev/null 2>&1
  wait $LPID
  echo "$w ($BUILD): $(tail -1 $T/load_$w.txt)"
  kill $SPID; wait $SPID 2>/dev/null
done
