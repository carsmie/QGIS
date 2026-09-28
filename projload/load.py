# Time QgsProject.read() with QGIS Server's read flags.
# usage: load.py <project.qgs> <repeats> [trust]   (trust: add TrustLayerMetadata + ForceReadOnlyLayers)
import os, sys, time
from qgis.core import QgsApplication, QgsProject, Qgis

app = QgsApplication([], False); app.initQgis()
path, n = sys.argv[1], int(sys.argv[2])
flags = (Qgis.ProjectReadFlag.DontStoreOriginalStyles | Qgis.ProjectReadFlag.DontLoad3DViews
         | Qgis.ProjectReadFlag.DontUpgradeAnnotations)
if len(sys.argv) > 3 and sys.argv[3] == 'trust':
    flags |= Qgis.ProjectReadFlag.TrustLayerMetadata | Qgis.ProjectReadFlag.ForceReadOnlyLayers
os.chdir(os.path.dirname(os.path.abspath(path)))
ts = []
for i in range(n):
    p = QgsProject()
    t0 = time.perf_counter()
    ok = p.read(path, flags)
    ts.append(time.perf_counter() - t0)
    assert ok, p.error()
    nl = len(p.mapLayers())
    del p
ts.sort()
print(f"{os.path.basename(path):18} {'trust' if len(sys.argv) > 3 else 'default':7} layers={nl:4d} "
      f"size={os.path.getsize(path)/1e6:5.1f} MB  min {ts[0]*1000:8.1f} ms  median {ts[len(ts)//2]*1000:8.1f} ms")
