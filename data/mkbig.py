from qgis.core import QgsApplication, QgsProject, QgsVectorLayer, QgsCoordinateReferenceSystem
app = QgsApplication([], False); app.initQgis()
p = QgsProject.instance(); p.setCrs(QgsCoordinateReferenceSystem('EPSG:25833'))
root = p.layerTreeRoot(); n = 0
for a in range(6):
    ga = root.addGroup(f'theme_{a}')
    for b in range(10):
        gb = ga.addGroup(f'theme_{a}_{b}')
        for c in range(10):
            v = QgsVectorLayer('lakes.gpkg|layername=lakes', f'l_{a}_{b}_{c}', 'ogr'); assert v.isValid()
            p.addMapLayer(v, False); gb.addLayer(v); n += 1
p.write('big.qgs'); print('layers:', n)
