# QGIS Server / GDAL performance work — handoff (2026-09-28)

Handoff from the macOS sessions to continue on another machine (Windows). This branch
(`perf-bench-kit`, orphan, no QGIS code) holds the benchmark kit and the notes. All code
is in the branches below.

## Where the code is

### QGIS — `github.com/carsmie/QGIS`

One linear stack on `upstream/master` (qgis/QGIS). Each branch contains all branches above it.

| Branch | Commit | Change |
|---|---|---|
| `optimize-project-perf-qt6` | `5ec4fe67292` | Faster project file read |
| | `042199adbae` | Faster project file save |
| | `db31a23191f` | macOS build fix (sqlite `.tbd`, AppKit) — **local only, keep out of upstream PRs** |
| | `814a0150510` | macOS: use same sqlite as SpatiaLite/GDAL — **local only**, only on this branch, not in the stack below |
| `wms-fast-png` | `62b49936fef` | `QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL` env var for faster PNG output |
| `hillshade-perf` | `311aefa2ef8` | Faster CPU hillshade renderer |
| `render-extent-cache` | `5ec12daaa2f` | Layer extent computed once per distinct layer transform |
| `crs-topocentric-cache` | `bc7c1ea4e1e` | Cache `QgsCoordinateReferenceSystem::topocentricOrigin()` |
| `wms-png-libdeflate` | `9d3f890a6be` | libdeflate for PNG output when available |
| `gdal-readblock-copy` | `b24513c418b` | No `memcpy()` per pixel in `QgsGdalProvider::readBlock()` |
| `ogr-load-fields-once` | `f13b88ab4b3` | OGR fields not reloaded when the encoding is unchanged |
| `wms-request-overhead` | `cd85290aed6` | WMS: `WMSUseLayerIDs` read once per request; restorer only resets what changed |
| `renderer-clone-fast` | `f0ee7fcbe68` | Cheaper clone of categorized/graduated renderers |
| `masking-collect-fast` | `c5ea540086e` | Skip the quadratic selective-masking lookup when no masking set can match |

The first stack (`db31a23` → `b24513c`) sits on top of `db31a23`, i.e. it includes the macOS
build-fix commit; drop it (`git rebase --onto`) before opening upstream PRs. No PRs opened yet.

The last four (`ogr-load-fields-once` … `masking-collect-fast`) are new today. Verified on macOS
(Release, patched GDAL): builds, and 212 GetMap / GetFeatureInfo responses over 5 test
projects are byte-identical to the baseline `gdal-readblock-copy`.

### GDAL — `github.com/carsmie/gdal`, branch `ogr-rect-filter` (commit `e803c62d3a`)

`OGRLayer::FilterGeometry()` / `FilterWKBGeometry()` decide rectangle spatial filters exactly
(segment/rectangle tests + even-odd rule, error-bounded orientation), instead of converting
the geometry to GEOS. Only when GEOS is available: without GEOS, all code paths keep accepting
any envelope intersection as before (making just one path exact broke CI on non-GEOS builds —
fixed and force-pushed today). Countries.fgb with tile-sized filters: 0.87 → 0.28 ms per tile.

CI (fork, branch `ci/ogr-rect-filter-3`): all green except two known non-patch issues:
armhf aborts after `gcore/vsiadls_real_instance.py` (same on upstream master), and `build-mac`
in the CMake workflow hangs in "Install dependency" on the fork (cancelled). To rerun CI, push
the commit to a **new** `ci/...` branch on the fork. No upstream PR yet.

## Results (macOS, M-series, Release; baseline = `gdal-readblock-copy`, same patched GDAL)

Mean per request, `bench.py`:

| Workload | Baseline | 4 new branches | Change |
|---|---|---|---|
| `catmap` — 120 layers, categorized, GetMap 256 px | 243 ms | 121 ms | **−50 %** |
| `gfi` — GetFeatureInfo JSON, same project | 139 ms | 96 ms | **−31 %** |
| `big` — 600-layer project, one layer | 3.80 ms | 3.65 ms | −4 % |
| `many` — 120 layers, simple styles | 74.2 ms | 73.4 ms | −1 % |
| `earth`, `relief` | 61 / 200 ms | 61 / 200 ms | none (expected) |

Two alternating runs per build, differences between runs < 3 %. Earlier numbers taken with the
macOS `sample` profiler attached were inflated (e.g. `many` looked like −15 %); use these.

Project loading (`projload/load.py`, median of 3): only `big.qgs` with trust flags clearly
improved (2.93 → 2.63 s); other differences were within the ±5–10 % noise.

## Open items

1. **Measure a real Kartverket project** (you said you'd do this on Windows). Needed to decide:
   - whether opening data sources (~47 % of project load in the synthetic 600-layer project,
     inflated by duplicate layers) is worth optimizing;
   - whether data is usually in the map CRS (EPSG:25833). If often reprojected, "simplify before
     reprojecting" (33 % of the earth workload) is worth it — it changes output by sub-pixel amounts.
2. **Undecided:** GetFeatureInfo spends 34 % cloning the categorized renderer per queried layer
   (`qgswmsrenderer.cpp`, `featureInfoFromVectorLayer`). `renderer-clone-fast` made clones
   cheaper; avoiding the clone is still open.
3. **Not pursuing:** WMS `STYLES=` style switching (XML round trip, 11–17× slower) — rarely used.
4. **Production setting, no code:** `QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL=1` (branch
   `wms-fast-png`) cut earth 73.6 → 17.9 ms, relief 222 → 37 ms.
5. Upstream PRs for QGIS stack and GDAL patch: not started. Everything must also work on
   Windows/Linux (see `notes/cross-platform-requirement.md`).

## Benchmark kit

```
python relocate.py [work_dir]      # once: fill in paths (work_dir holds QGIS/ and gdal/ clones)
python fetch_data.py               # with a python that imports qgis.core, GDAL tools on PATH
python bench.py <qgis_mapserver> [earth relief many big gfi catmap]
python tools/cmp.py <base_mapserver> <new_mapserver> [earth relief many big cat]   # byte-compare
python projload/load.py data/big.qgs 3 [trust]                                     # project load time
```

`fetch_data.py` downloads Natural Earth 10m (countries, boundary lines, lakes) and a
Copernicus DEM 30m tile around Oslo, then generates:

| Project | Content | Workloads |
|---|---|---|
| `data/earth_local.qgs` | 3 NE layers (in git) | `earth`: 1184 px tiles, 4 zoom levels |
| `data/relief.qgs` | hillshade DEM + 2 vector layers | `relief` |
| `data/many.qgs` | 120 layers in 12 groups | `many`: 256 px tiles |
| `data/big.qgs` | 600 layers, nested groups | `big`: one layer, 256 px |
| `xmlbench/styles_cat.qgs` | 120 layers, ~258 categories on countries | `catmap`, `gfi` (GetFeatureInfo JSON) |

On Windows, the `.sh` scripts (`*/prof.sh`, `projload/run.sh`, `tools/*.sh`, `xmlbench/setup.sh`)
are the macOS originals (zsh, `sample` profiler, Homebrew paths) — kept for reference only; the
`.py` scripts above are portable. `data/*.cpp` are micro-benchmarks for PNG/hillshade/format
work, `gdal/harness.cpp` the GDAL rectangle-filter harness.

## Notes

`notes/` has the Claude memory notes from the macOS machine (build setup quirks, GDAL fork
notes, decisions). They describe the Mac; on Windows, treat build details as background.
