---
name: qgis-xml-style-switching
description: "QGIS Server XML style switching is slow but user rarely uses WMS STYLES=; don't pursue it"
metadata:
  node_type: memory
  type: project
  originSessionId: 260d7884-cd4d-4e63-8737-785263ada196
  modified: 2026-09-28T11:46:42.701Z
---

Measured 2026-09-28 (120-layer benchmark, build-gdalrect): GetMap with non-default STYLES= is 11–17x slower (86→985 ms simple styles, 242→4247 ms categorized) because QgsMapLayerStyleManager::setCurrentStyle round-trips the whole style through QDomDocument (96% of request). Normal GetMap uses no XML; GetFeatureInfo spends ~10% on XML, but 34% on cloning the categorized renderer per queried layer (QgsWmsRenderer featureInfoFromVectorLayer, qgswmsrenderer.cpp ~line 1953).

**Why:** user said they rarely use STYLES=, so no work wanted on style switching.

**How to apply:** don't propose the XML style-switch fix again unless their usage changes. The GetFeatureInfo renderer-clone cost was offered but not yet decided. Related: [[gdal-fork-rect-filter]].
