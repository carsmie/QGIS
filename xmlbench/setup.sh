#!/bin/zsh
S=@KIT@/data
X=@KIT@/xmlbench
G=@WORK@/gdal-ogr-rect-filter
O=@WORK@/QGIS/build-gdalrect/output
cp $S/many.qgs $S/countries.fgb $S/boundaries.fgb $S/lakes.fgb $X/
cd $X
PYTHONPATH=$O/python:$G/lib/python3.14/site-packages QT_QPA_PLATFORM=offscreen /opt/homebrew/opt/python@3.14/bin/python3.14 mkstyles.py 2>&1 | grep -v "^Warning\|QStandardPaths" | tail -4
ls -la *.qgs | awk '{print $5, $9}'
