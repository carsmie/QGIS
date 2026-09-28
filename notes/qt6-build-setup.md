---
name: qt6-build-setup
description: "How the local macOS Qt6 build of upstream QGIS master is configured (build-qt6/), incl. non-obvious workarounds"
metadata:
  node_type: memory
  type: project
  originSessionId: 963fe9e3-70ba-4bd4-a1a2-8a81d776caef
  modified: 2026-09-27T11:38:35.878Z
---

Since 2026-09-27 the user works on upstream-based branch `optimize-project-perf-qt6`; upstream QGIS master is Qt6-only (Qt >= 6.4, C++20). The old Qt5 setup (`build/`, Qt6 kegs unlinked) is obsolete.

- Homebrew Qt6 kegs are now **linked** (qtbase, qtsvg, qt3d, qtpositioning, qtserialport, qttools, qwt, qca, pyqt). This breaks the old Qt5 `build/` dir.
- Build dir `build-qt6/` (Ninja, RelWithDebInfo). Non-obvious flags:
  - `-DWITH_INTERNAL_SPATIALINDEX=ON` — upstream CMake rejects Homebrew spatialindex >= 2.1; Homebrew `spatialindex` must also be **unlinked**, else its 2.1 headers in /opt/homebrew/include shadow the vendored 2.0 ones (LeafQueryResult incomplete-type errors)
  - `-DUSE_OPENCL=OFF` — upstream src/core/CMakeLists.txt switched to `OpenCL::OpenCL` and lost the vendored `external/opencl-clhpp` include dir, so `CL/cl2.hpp` isn't found on macOS
  - `BISON_EXECUTABLE`/`FLEX_EXECUTABLE` = `/opt/homebrew/opt/{bison,flex}/bin/*` (keg-only; system bison 2.3 too old)
  - `Python_EXECUTABLE=/opt/homebrew/opt/python@3.14/bin/python3.14`, `WITH_QTWEBENGINE=OFF`, GRASS/PDAL off
  - `WITH_SERVER=ON` (needs brew `fcgi`) plus `QGIS_SERVER_MODULE_SUBDIR=Contents/MacOS/server` and `QGIS_CGIBIN_SUBDIR=Contents/MacOS` — upstream sets no macOS bundle default, so configure fails with "install TARGETS given no LIBRARY DESTINATION"
  - `SIP_BUILD_EXECUTABLE=/opt/homebrew/opt/sip/bin/sip-build` (brew sip 6.16 venv) — the pip sip 6.14 crashes in the resolver
- sip 6.14 was pip-installed into Homebrew python3.14 and owns `/opt/homebrew/bin/sip-build`; brew `sip`/`pyqt-builder` are installed but deliberately left unlinked.
- macOS build tweaks (`cmake/FindSqlite3.cmake` .tbd fix, AppKit/Foundation linking in `src/native/CMakeLists.txt`) are committed on the branch as "Fix macOS build with SDK sqlite3 .tbd and AppKit linking".

**Why:** upstream dropped Qt5; these were the blockers found configuring on this Mac.
**How to apply:** build with `cmake --build build-qt6 --target qgis_core`; don't unlink Qt6 or reuse `build/`.
