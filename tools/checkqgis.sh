#!/bin/zsh
O=@WORK@/QGIS/build-gdalrect/output
echo "desktop links: $(otool -L $O/Contents/MacOS/qgis | grep -i libgdal | awk '{print $1}')"
QT_QPA_PLATFORM=offscreen $O/Contents/MacOS/qgis_process --version 2>&1 | grep -iE "qgis|gdal|geos" | head -5
cat > @KIT@/tools/pycheck.py <<'EOF'
from qgis.core import Qgis, QgsApplication, QgsVectorLayer
from osgeo import gdal
app = QgsApplication([], False); app.initQgis()
print("qgis.core", Qgis.version(), "| osgeo.gdal", gdal.__version__, gdal.__file__)
lyr = QgsVectorLayer("@KIT@/data/countries.fgb", "c", "ogr")
print("layer valid:", lyr.isValid(), "features:", lyr.featureCount())
EOF
PYTHONPATH=$O/python QT_QPA_PLATFORM=offscreen /opt/homebrew/opt/python@3.14/bin/python3.14 @KIT@/tools/pycheck.py 2>&1 | tail -4
