from qgis.core import (QgsApplication, QgsProject, QgsRasterLayer, QgsVectorLayer, QgsHillshadeRenderer,
                       QgsCoordinateReferenceSystem)
app = QgsApplication([], False); app.initQgis()
p = QgsProject.instance(); p.setCrs(QgsCoordinateReferenceSystem('EPSG:25833'))
dem = QgsRasterLayer('dem.tif', 'relief'); assert dem.isValid()
r = QgsHillshadeRenderer(dem.dataProvider(), 1, 315, 45); r.setZFactor(1.5); r.setMultiDirectional(True)
r.setOpacity(0.55)   # semi-transparent relief overlay, like a typical relief WMS
dem.setRenderer(r); p.addMapLayer(dem)
for name in ('lakes', 'boundaries'):
    v = QgsVectorLayer(f'{name}.fgb', name, 'ogr'); assert v.isValid(); p.addMapLayer(v)
p.writeEntry('WMSImageQuality', '/', 90)
p.write('relief.qgs'); print('ok', [l.name() for l in p.mapLayers().values()])
