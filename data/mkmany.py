from qgis.core import QgsApplication, QgsProject, QgsVectorLayer, QgsCoordinateReferenceSystem, QgsLayerTreeGroup
app = QgsApplication([], False); app.initQgis()
p = QgsProject.instance(); p.setCrs(QgsCoordinateReferenceSystem('EPSG:25833'))
srcs = ['lakes.fgb', 'boundaries.fgb', 'countries.fgb']
root = p.layerTreeRoot()
for g in range(12):
    grp = root.addGroup(f'group_{g}')
    for i in range(10):
        v = QgsVectorLayer(srcs[(g * 10 + i) % 3], f'layer_{g}_{i}', 'ogr'); assert v.isValid()
        v.setScaleBasedVisibility(False)
        p.addMapLayer(v, False); grp.addLayer(v)
p.writeEntry('WMSImageQuality', '/', 90)
p.write('many.qgs'); print('layers:', len(p.mapLayers()))
