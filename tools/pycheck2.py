import os, subprocess
from qgis.core import Qgis, QgsApplication, QgsVectorLayer
from osgeo import gdal
app = QgsApplication([], False); app.initQgis()
print("qgis.core", Qgis.version(), "| osgeo.gdal", gdal.__version__, gdal.__file__)
lyr = QgsVectorLayer("@KIT@/data/countries.fgb", "c", "ogr")
print("layer valid:", lyr.isValid(), "features:", lyr.featureCount())
out = subprocess.run(["vmmap", str(os.getpid())], capture_output=True, text=True).stdout
print("libgdal mapped:", sorted({w for l in out.splitlines() for w in l.split() if "libgdal" in w}))
