from qgis.core import QgsApplication, QgsProject, QgsRasterLayer, QgsCoordinateReferenceSystem
app = QgsApplication([], False); app.initQgis()
p = QgsProject.instance(); p.setCrs(QgsCoordinateReferenceSystem('EPSG:25833'))
for f in ('dem_byte.tif', 'dem_u16.tif', 'dem_f64.tif', 'dem.tif'):
    l = QgsRasterLayer(f, f.split('.')[0], 'gdal'); assert l.isValid(), f; p.addMapLayer(l)
p.write('rasters.qgs'); print('ok')
