#!/bin/bash
# Configure the server-perf2 worktree build like QGIS/build-gdalrect (patched GDAL).
set -e
Q=@WORK@/QGIS
W=$Q/.claude/worktrees/server-perf2
G=@WORK@/gdal-ogr-rect-filter
export PATH=/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH
OPTS=()
while IFS= read -r line; do
  OPTS+=("$line")
done < <(grep -E "^(WITH_[A-Z0-9_]+|ENABLE_TESTS|QGIS_[A-Z_]*SUBDIR|BUILD_WITH_QT6|USE_OPENCL|USE_CCACHE|CMAKE_INSTALL_PREFIX|SIP_BUILD_EXECUTABLE|Python_EXECUTABLE):[A-Z]+=" "$Q/build-gdalrect/CMakeCache.txt" | sed -E 's/^([^:]+):[A-Z]+=(.*)$/-D\1=\2/')
echo "copied ${#OPTS[@]} options from build-gdalrect"
mkdir -p "$W/build"
cd "$W/build"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_PREFIX_PATH=$G;/opt/homebrew;/opt/homebrew/opt/sqlite;/opt/homebrew/opt/libpq;/opt/homebrew/opt/bison;/opt/homebrew/opt/flex" \
  "-DGDAL_DIR=$G/lib/cmake/gdal" \
  "-DCMAKE_CXX_FLAGS=-I$G/include-first" "-DCMAKE_C_FLAGS=-I$G/include-first" \
  "${OPTS[@]}" .. > cmake.log 2>&1
echo "cmake exit=$?"
grep -E "Found GDAL" cmake.log
