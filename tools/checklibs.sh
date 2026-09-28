#!/bin/zsh
# Start the patched server, render one tile, list libgdal images mapped in the process.
cd @KIT@/data
@WORK@/QGIS/build-gdalrect/output/Contents/MacOS/qgis_mapserver -l 1 -p many.qgs 127.0.0.1:8842 > /dev/null 2>&1 &
SPID=$!
for i in {1..40}; do lsof -nP -iTCP:8842 -sTCP:LISTEN -t >/dev/null 2>&1 && break; sleep 0.5; done
curl -s -o /dev/null "http://127.0.0.1:8842/?SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&STYLES=&SRS=EPSG:25833&FORMAT=image/png&WIDTH=256&HEIGHT=256&LAYERS=group_0&BBOX=300000,7000000,301354,7001354"
vmmap $SPID 2>/dev/null | grep -o "/[^ ]*libgdal[^ ]*" | sort -u
kill $SPID
