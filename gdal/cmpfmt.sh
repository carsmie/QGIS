#!/bin/bash
T=@KIT@
NEWBIN=@WORK@/gdal-ogr-rect-filter/bin
"$NEWBIN/gdalinfo" --version
("$NEWBIN/gdalinfo" --formats; "$NEWBIN/ogrinfo" --formats) | tail -n +2 | grep -v "^Supported" | awk '{print $1}' | sort -u > $T/fmt_new.txt
(/opt/homebrew/bin/gdalinfo --formats; /opt/homebrew/bin/ogrinfo --formats) | tail -n +2 | grep -v "^Supported" | awk '{print $1}' | sort -u > $T/fmt_brew.txt
echo "new: $(wc -l < $T/fmt_new.txt) drivers, homebrew: $(wc -l < $T/fmt_brew.txt) drivers"
echo "only in homebrew: $(comm -13 $T/fmt_new.txt $T/fmt_brew.txt | tr '\n' ' ')"
echo "only in new: $(comm -23 $T/fmt_new.txt $T/fmt_brew.txt | tr '\n' ' ')"
otool -L @WORK@/gdal-ogr-rect-filter/lib/libgdal.dylib | grep -iE "sqlite|libgdal"
