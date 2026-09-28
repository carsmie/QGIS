#!/bin/bash
# Configure QGIS/build-gdalrect like build-release, but against the patched GDAL.
set -e
Q=@WORK@/QGIS
G=@WORK@/gdal-ogr-rect-filter
export PATH=/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH
OPTS=()
while IFS= read -r line; do
  OPTS+=("$line")
done < <(grep -E "^(WITH_[A-Z0-9_]+|ENABLE_TESTS|QGIS_[A-Z_]*SUBDIR|BUILD_WITH_QT6|USE_OPENCL|USE_CCACHE|CMAKE_INSTALL_PREFIX):[A-Z]+=" "$Q/build-release/CMakeCache.txt" | sed -E 's/^([^:]+):[A-Z]+=(.*)$/-D\1=\2/')
echo "copied ${#OPTS[@]} options from build-release"
mkdir -p "$Q/build-gdalrect"
cd "$Q/build-gdalrect"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$G;/opt/homebrew;/opt/homebrew/opt/sqlite;/opt/homebrew/opt/libpq;/opt/homebrew/opt/bison;/opt/homebrew/opt/flex" \
  "-DGDAL_DIR=$G/lib/cmake/gdal" \
  "${OPTS[@]}" .. > cmake.log 2>&1
echo "cmake exit=$?"
grep -E "^GDAL_DIR|^GDAL_" CMakeCache.txt | head -5
grep -iE "Found GDAL|GDAL version|gdal" cmake.log | head -5
