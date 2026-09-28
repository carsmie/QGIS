#!/bin/zsh
G=@WORK@/gdal-ogr-rect-filter
O=@WORK@/QGIS/build-gdalrect/output
echo "osgeo _gdal links: $(otool -L $G/lib/python3.14/site-packages/osgeo/_gdal*.so | grep libgdal | awk '{print $1}')"
cat > @KIT@/tools/pycheck2.py <<'EOF'
import os, subprocess
from qgis.core import Qgis, QgsApplication, QgsVectorLayer
from osgeo import gdal
app = QgsApplication([], False); app.initQgis()
print("qgis.core", Qgis.version(), "| osgeo.gdal", gdal.__version__, gdal.__file__)
lyr = QgsVectorLayer("@KIT@/data/countries.fgb", "c", "ogr")
print("layer valid:", lyr.isValid(), "features:", lyr.featureCount())
out = subprocess.run(["vmmap", str(os.getpid())], capture_output=True, text=True).stdout
print("libgdal mapped:", sorted({w for l in out.splitlines() for w in l.split() if "libgdal" in w}))
EOF
PYTHONPATH=$O/python:$G/lib/python3.14/site-packages QT_QPA_PLATFORM=offscreen /opt/homebrew/opt/python@3.14/bin/python3.14 @KIT@/tools/pycheck2.py 2>&1 | tail -4
