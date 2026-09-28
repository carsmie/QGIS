#!/bin/zsh
# Profile project loading: prof.sh <project.qgs> <name> [trust]
G=@WORK@/gdal-ogr-rect-filter
O=@WORK@/QGIS/${BUILD:-build-gdalrect}/output
T=@KIT@/projload
export PYTHONPATH=$O/python:$G/lib/python3.14/site-packages QT_QPA_PLATFORM=offscreen
/opt/homebrew/opt/python@3.14/bin/python3.14 $T/load.py $1 6 $3 > $T/prof_$2.txt 2>&1 &
LPID=$!
sleep 3
sample $LPID 15 -mayDie -file $T/sample_$2.txt > /dev/null 2>&1
wait $LPID
grep -v "^Warning\|QStandardPaths" $T/prof_$2.txt
