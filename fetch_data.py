# Download the benchmark data and generate the test projects (run relocate.py first).
# usage: python fetch_data.py
# Must run with a python that can import qgis.core (e.g. OSGeo4W shell, or PYTHONPATH=<build>/output/python),
# with the GDAL command line tools (ogr2ogr, gdalwarp, gdal_translate) on PATH.
import os, subprocess, sys, shutil, urllib.request

KIT = "@KIT@"
D = os.path.join(KIT, "data")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen")


def run(*cmd, cwd=D):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


os.chdir(D)
# Natural Earth 10m (public domain)
for n in ("cultural/ne_10m_admin_0_countries", "cultural/ne_10m_admin_0_boundary_lines_land", "physical/ne_10m_lakes"):
    z = os.path.basename(n) + ".zip"
    if not os.path.exists(z):
        urllib.request.urlretrieve(f"https://naciscdn.org/naturalearth/10m/{n}.zip", z)
run("ogr2ogr", "-overwrite", "-f", "FlatGeobuf", "countries.fgb", "/vsizip/ne_10m_admin_0_countries.zip",
    "-nln", "countries", "-nlt", "PROMOTE_TO_MULTI")
run("ogr2ogr", "-overwrite", "-f", "FlatGeobuf", "boundaries.fgb", "/vsizip/ne_10m_admin_0_boundary_lines_land.zip",
    "-nlt", "PROMOTE_TO_MULTI")
run("ogr2ogr", "-overwrite", "-f", "FlatGeobuf", "lakes.fgb", "/vsizip/ne_10m_lakes.zip", "-nlt", "PROMOTE_TO_MULTI")
for name in ("countries", "boundaries", "lakes"):
    run("ogr2ogr", "-overwrite", "-f", "GPKG", f"{name}.gpkg", f"{name}.fgb", "-nln", name)

# Copernicus DEM 30m tile around Oslo, warped to EPSG:25833 at 25 m (2400x2400)
if not os.path.exists("dem.tif"):
    run("gdalwarp", "-q", "-overwrite", "-t_srs", "EPSG:25833", "-tr", "25", "25",
        "-te", "230000", "6620000", "290000", "6680000", "-r", "bilinear",
        "/vsicurl/https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N59_00_E010_00_DEM/"
        "Copernicus_DSM_COG_10_N59_00_E010_00_DEM.tif", "dem.tif")
run("gdal_translate", "-q", "-ot", "Byte", "-scale", "-10", "680", "0", "255", "dem.tif", "dem_byte.tif")
run("gdal_translate", "-q", "-ot", "UInt16", "-scale", "-10", "680", "0", "65535", "dem.tif", "dem_u16.tif")
run("gdal_translate", "-q", "-ot", "Float64", "dem.tif", "dem_f64.tif")

# many.qgs (120 layers), big.qgs (600 layers), relief.qgs, rasters.qgs; earth_local.qgs is in git
for s in ("mkmany", "mkbig", "mkrelief", "mkrast"):
    run(sys.executable, f"{s}.py")

# styles_simple.qgs / styles_cat.qgs (120 layers, ~258 categories on the countries layers)
X = os.path.join(KIT, "xmlbench")
for f in ("many.qgs", "countries.fgb", "boundaries.fgb", "lakes.fgb"):
    shutil.copy(f, X)
run(sys.executable, "mkstyles.py", cwd=X)
print("done")
