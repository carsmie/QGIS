---
name: gdal-fork-rect-filter
description: GDAL fork (carsmie/gdal) work on fast rectangle spatial filters for QGIS Server tiles; build pitfalls on this Mac
metadata:
  node_type: memory
  type: project
  originSessionId: 260d7884-cd4d-4e63-8737-785263ada196
  modified: 2026-09-28T10:58:14.191Z
---

User forked OSGeo/gdal to carsmie/gdal (clone at ~/Projekte/kartverket/gdal). On 2026-09-28 branch `ogr-rect-filter` was pushed (now commit e803c62d3a, force-pushed 2026-09-28 after a CI fix): OGRLayer::FilterGeometry/FilterWKBGeometry decide rectangle filters without GEOS (exact, GEOS fallback on doubt). Motivated by QGIS Server profiling where FilterGeometry was ~50% of request time (see [[qgis-server-perf-branches]]). User said it matters for some of their (Kartverket) data with large polygons / long lines, not only the 180° case. Not yet proposed upstream as a PR.

**Why:** big features whose bbox covers a tile but have no vertex in it forced full GEOS conversion per tile.

Full-driver patched GDAL (+ osgeo Python bindings) is installed at ~/Projekte/kartverket/gdal-ogr-rect-filter; QGIS built against it in QGIS/build-gdalrect (start via build-gdalrect/run-qgis.sh). Local macOS-only workarounds: imath include for EXR driver, absolute install names, and QGIS needs `-I<prefix>/include-first` (a *copy* of the headers) because clang drops an -I dir that is also -isystem, so /opt/homebrew/include's GDAL 3.13 headers won otherwise; QGIS must use SIP from /opt/homebrew/opt/sip (pip sip 6.14 crashes). Fork CI runs by pushing to a fresh `ci/...` branch (ci/ogr-rect-filter-3 was green). The exact rectangle test is gated on haveGEOS(): without GEOS, FilterWKBGeometry() and driver fast counts stay envelope-only, so making only FilterGeometry() exact broke non-GEOS CI (Intel, build-windows-minimum). Known non-patch CI failures: armhf aborts after gcore/vsiadls_real_instance.py (upstream master too); `build-mac` in the CMake workflow hangs in 'Install dependency' on the fork (upstream finishes in ~15 min). See [[cross-platform-requirement]].

**How to apply:** when building GDAL here, pass `-DSQLite3_INCLUDE_DIR=/opt/homebrew/opt/sqlite/include -DSQLite3_LIBRARY=/opt/homebrew/opt/sqlite/lib/libsqlite3.dylib`, otherwise it links macOS system SQLite and GPKG crashes (null function call). No swig installed, so Python autotests can't run; use the C++ gdal_unit_test (3 tests — TileMatrixSet, two NAD83 State Plane — fail at baseline too).
