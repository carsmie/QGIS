#!/bin/zsh
# Profile one request mode with macOS `sample`: prof.sh <project> <mode> <n>
X=@KIT@/xmlbench
Q=@WORK@/QGIS/build-gdalrect/output/Contents/MacOS/qgis_mapserver
PY=python3
cd $X
$Q -l 1 -p $X/$1.qgs 127.0.0.1:8851 > /dev/null 2>&1 &
SPID=$!
for i in {1..240}; do lsof -nP -iTCP:8851 -sTCP:LISTEN -t >/dev/null 2>&1 && break; sleep 0.5; done
$PY load.py 8851 $2 3 > /dev/null
$PY load.py 8851 $2 $3 > $X/prof_load.txt 2>&1 &
LPID=$!
sleep 1
sample $SPID 15 -mayDie -file $X/sample_$1_$2.txt > /dev/null 2>&1
wait $LPID
kill $SPID; wait $SPID 2>/dev/null
echo "sample written: sample_$1_$2.txt"
