#!/bin/zsh
# Time project loading for several projects, with and without trust flags.
S=@KIT@/data
X=@KIT@/xmlbench
G=@WORK@/gdal-ogr-rect-filter
O=@WORK@/QGIS/${BUILD:-build-gdalrect}/output
export PYTHONPATH=$O/python:$G/lib/python3.14/site-packages QT_QPA_PLATFORM=offscreen
PYQ=/opt/homebrew/opt/python@3.14/bin/python3.14
L=@KIT@/projload/load.py
for p in $S/many.qgs $S/big.qgs $X/styles_cat.qgs; do
  $PYQ $L $p 3 2>&1 | grep -v "^Warning\|QStandardPaths"
  $PYQ $L $p 3 trust 2>&1 | grep -v "^Warning\|QStandardPaths"
done
