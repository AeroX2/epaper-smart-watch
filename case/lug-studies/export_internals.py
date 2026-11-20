"""Export the display, PCB and battery from case.FCStd for the lug-studies viewer.

    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" -c "exec(open(r'case/lug-studies/export_internals.py').read())"

Writes internals.json (meshes for the viewer's Internals toggle) and
internals-points.json (dense surface samples for check_clearance.js).
The display's flat cable is drawn unfolded in the CAD, so everything at X < -20.6 is left out.
"""
import json
import os

import FreeCAD as App

HERE = r"C:\Users\James\Documents\git\epaper-smart-watch\case\lug-studies"
doc = App.openDocument(os.path.join(os.path.dirname(HERE), "case.FCStd"))
meshes, points = {}, {}
for name, label in (("Display_merged", "display"), ("PCB_merged", "pcb"), ("Body001", "battery")):
    shape = doc.getObject(name).Shape
    v, f = shape.tessellate(0.25)
    keep = [t for t in f if all(v[i].x >= -20.6 for i in t)] if label == "display" else f
    # viewer axes: x along the strap, y up, z across
    meshes[label] = {"p": [round(c, 2) for p in v for c in (p.x, p.z, -p.y)], "i": [i for t in keep for i in t]}
    pts = set()
    v2, _ = shape.tessellate(0.1)
    for p in v2:
        pts.add((round(p.x, 2), round(p.y, 2), round(p.z, 2)))
    for e in shape.Edges:
        try:
            for p in e.discretize(max(2, int(e.Length / 0.5))):
                pts.add((round(p.x, 2), round(p.y, 2), round(p.z, 2)))
        except Exception:
            pass
    points[label] = sorted(pts)
json.dump(meshes, open(os.path.join(HERE, "internals.json"), "w"), separators=(",", ":"))
json.dump(points, open(os.path.join(HERE, "internals-points.json"), "w"), separators=(",", ":"))
