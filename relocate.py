# Replace the path placeholders in the kit files. Run once after cloning.
#   @KIT@  -> this directory
#   @WORK@ -> directory holding the QGIS and gdal clones (default: ~/Projekte/kartverket)
# usage: python relocate.py [work_dir]
import os, sys

kit = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")
work = (sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Projekte/kartverket")).replace("\\", "/")
for root, dirs, files in os.walk(kit):
    dirs[:] = [d for d in dirs if d != ".git"]
    for f in files:
        if f in ("relocate.py", "README.md"):
            continue
        p = os.path.join(root, f)
        try:
            s = open(p, encoding="utf-8").read()
        except UnicodeDecodeError:
            continue
        if "@KIT@" in s or "@WORK@" in s:
            open(p, "w", encoding="utf-8", newline="").write(s.replace("@KIT@", kit).replace("@WORK@", work))
            print("updated", os.path.relpath(p, kit))
print("KIT =", kit, " WORK =", work)
