"""Tie the lid's 0.5 mm screw recess (Pocket029 / Sketch066) to the screw parameters.

Sketch066 used to project its circles from Binder005, a frozen copy of an old case
face, so it stayed at the old hole positions when ScrewY moved. Run after
apply_screw_inserts_to_fcstd.py. Set FCSTD and RESULT before exec()ing.
"""
import FreeCAD as App
import Part
import Sketcher

P = "ButtonParameters."
doc = App.openDocument(FCSTD)
vs = doc.getObject("ButtonParameters")
if not hasattr(vs, "LidRecessDiameter"):
    vs.addProperty("App::PropertyLength", "LidRecessDiameter", "Screw inserts",
                   "0.5 mm recess around each screw on the lid's outer face (Pocket029)")
    vs.LidRecessDiameter = 3.2

sk = doc.getObject("Sketch066")
while sk.ExternalGeometry:
    sk.delExternal(0)
for k, (sx, sy) in enumerate(((1, 1), (-1, 1), (1, -1), (-1, -1))):
    sk.addGeometry(Part.Circle(App.Vector(sx * 19.23, sy * 11.385, 0), App.Vector(0, 0, 1), 1.6), False)
    i = sk.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, k, 3, sx * 19.23))
    sk.renameConstraint(i, "X%d" % k)
    sk.setExpression("Constraints.X%d" % k, ("" if sx > 0 else "-") + P + "ScrewX")
    i = sk.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, k, 3, sy * 11.385))
    sk.renameConstraint(i, "Y%d" % k)
    sk.setExpression("Constraints.Y%d" % k, ("" if sy > 0 else "-") + P + "ScrewY")
    if k == 0:
        i = sk.addConstraint(Sketcher.Constraint("Diameter", 0, 3.2))
        sk.renameConstraint(i, "RecessDiameter")
        sk.setExpression("Constraints.RecessDiameter", P + "LidRecessDiameter")
    else:
        sk.addConstraint(Sketcher.Constraint("Equal", 0, k))
doc.recompute()

log = open(RESULT, "w")
log.write("bad: %s\n" % [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State])
log.write("Sketch066 fully=%s placement=%s\n" % (sk.FullyConstrained, sk.Placement))
for n in ("Pocket029", "Pocket030"):
    for f in doc.getObject(n).Shape.Faces:
        s = f.Surface
        if isinstance(s, Part.Cylinder) and s.Radius < 2.5:
            log.write("  %s cyl r=%.3f axis (%.3f,%.3f) z %.2f..%.2f\n" % (
                n, s.Radius, s.Center.x, s.Center.y, f.BoundBox.ZMin, f.BoundBox.ZMax))
for n in ("Pocket029", "Pocket030", "Pad007", "Fillet", "Pocket031", "Pocket032"):
    o = doc.getObject(n)
    log.write(" %s vol=%.3f valid=%s\n" % (n, o.Shape.Volume, o.Shape.isValid()))
log.close()
