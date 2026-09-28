# Build test projects from the 120-layer benchmark project:
#   styles_simple.qgs : original styles + identical named style "alt" on every layer
#   styles_cat.qgs    : countries layers categorized by ADMIN (~258 classes),
#                       + identical named style "alt" on every layer
import random
from qgis.core import (QgsApplication, QgsProject, QgsMapLayerStyle, QgsCategorizedSymbolRenderer,
                       QgsRendererCategory, QgsSymbol, QgsVectorLayer)
from qgis.PyQt.QtGui import QColor

app = QgsApplication([], False); app.initQgis()

def add_alt(p):
    for lyr in p.mapLayers().values():
        sm = lyr.styleManager()
        st = QgsMapLayerStyle(); st.readFromLayer(lyr)
        assert sm.addStyle('alt', st)

for variant in ('simple', 'cat'):
    p = QgsProject.instance()
    p.clear()
    assert p.read('many.qgs')
    if variant == 'cat':
        rnd = random.Random(1)
        for lyr in p.mapLayers().values():
            if lyr.source().endswith('countries.fgb'):
                idx = lyr.fields().indexOf('ADMIN')
                cats = []
                for v in sorted(lyr.uniqueValues(idx)):
                    s = QgsSymbol.defaultSymbol(lyr.geometryType())
                    s.setColor(QColor(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)))
                    cats.append(QgsRendererCategory(v, s, v))
                lyr.setRenderer(QgsCategorizedSymbolRenderer('ADMIN', cats))
    add_alt(p)
    for lyr in p.mapLayers().values():
        lyr.setFlags(lyr.flags() | lyr.Identifiable)
    assert p.write(f'styles_{variant}.qgs')
    lyr = next(l for l in p.mapLayers().values() if l.source().endswith('countries.fgb'))
    st = QgsMapLayerStyle(); st.readFromLayer(lyr)
    print(variant, 'layers:', len(p.mapLayers()), '| countries style XML bytes:', len(st.xmlData()),
          '| styles:', lyr.styleManager().styles())
