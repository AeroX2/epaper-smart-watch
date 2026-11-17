"""Thicken the strap-lug arms and give the screw holes a stepped heat-set insert bore.

Set FCSTD (path) and RESULT (log path) in the globals before exec()ing this file.
New parameters live in the "Screw inserts" group of the ButtonParameters VarSet.

Insert: 3.5 mm OD x 3.2 mm tall heat-set. Screw: 6 mm overall, 2 mm head (4 mm dia),
so 4 mm of shank: 1 mm through the lid leaves ~3 mm in the insert.
Bore (from the rim): lead-in dia 3.7 x 1.0 to stand the insert up straight, then
grip dia 3.2 for the rest of the insert, total depth 3.8 so the insert can sit flush
with room for displaced plastic.
"""
import FreeCAD as App
import Part
import Sketcher

P = "ButtonParameters."
PARAMS = [
    ("LugArmThickening", 1.8, "How far the outer edge of the four lug arms moves out"),
    ("ScrewX", 19.23, "Screw hole centre |x|"),
    ("ScrewY", 11.385, "Screw hole centre |y| (centred in the thickened arm block)"),
    ("InsertGripDiameter", 3.2, "Bore the insert melts into (insert OD 3.5)"),
    ("InsertLeadInDiameter", 3.7, "Alignment bore at the rim, slightly over insert OD"),
    ("InsertLeadInDepth", 1.0, "Depth of the alignment bore"),
    ("InsertHoleDepth", 3.8, "Total bore depth from the rim (insert is 3.2 tall)"),
]

doc = App.openDocument(FCSTD)
body = doc.getObject("Body")
vs = doc.getObject("ButtonParameters")
for name, value, text in PARAMS:
    vs.addProperty("App::PropertyLength", name, "Screw inserts", text)
    setattr(vs, name, value)

# 1. lug arms: end thickness (was 1.0) and arm/body join height (span was 26 = 2 x 13)
outline = doc.getObject("Sketch")
assert outline.Constraints[42].Type == "Distance" and abs(outline.Constraints[42].Value - 1.0) < 1e-6
assert outline.Constraints[55].Type == "DistanceY" and abs(outline.Constraints[55].Value - 26.0) < 1e-6
outline.renameConstraint(42, "LugArmEnd")
outline.renameConstraint(55, "LugArmJoinSpan")
outline.setExpression("Constraints.LugArmEnd", "1 mm + %sLugArmThickening" % P)
outline.setExpression("Constraints.LugArmJoinSpan", "26 mm + 2 * %sLugArmThickening" % P)

# 2. existing grip bore: position it, drive diameter and depth
holes = doc.getObject("Sketch059")
i = holes.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, 0, 3, 19.23))
holes.renameConstraint(i, "ScrewX")
i = holes.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, 0, 3, 10.52))
holes.renameConstraint(i, "ScrewY")
holes.renameConstraint(0, "GripDiameter")
holes.setExpression("Constraints.ScrewX", P + "ScrewX")
holes.setExpression("Constraints.ScrewY", P + "ScrewY")
holes.setExpression("Constraints.GripDiameter", P + "InsertGripDiameter")
grip = doc.getObject("Pocket028")
grip.setExpression("Length", P + "InsertHoleDepth")
grip.Label = "InsertGripBore"

# 3. lid clearance holes: were locked to external edges that move with the outline;
#    drive them from the same parameters instead
lidholes = doc.getObject("Sketch067")
while lidholes.ExternalGeometry:
    lidholes.delExternal(0)
for k in range(4):
    c = lidholes.Geometry[k].Center
    sx, sy = (1 if c.x > 0 else -1), (1 if c.y > 0 else -1)
    i = lidholes.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, k, 3, sx * 19.23))
    lidholes.renameConstraint(i, "X%d" % k)
    lidholes.setExpression("Constraints.X%d" % k, ("" if sx > 0 else "-") + P + "ScrewX")
    i = lidholes.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, k, 3, sy * 10.52))
    lidholes.renameConstraint(i, "Y%d" % k)
    lidholes.setExpression("Constraints.Y%d" % k, ("" if sy > 0 else "-") + P + "ScrewY")

# 4. lead-in bore at the rim
lead = body.newObject("Sketcher::SketchObject", "InsertLeadInSketch")
lead.MapMode = "Deactivated"
lead.setExpression("Placement", "placement(vector(0 mm; 0 mm; %sRimHeight); rotation(0; 0; 0))" % P)
for k, (sx, sy) in enumerate(((1, 1), (-1, 1), (1, -1), (-1, -1))):
    lead.addGeometry(Part.Circle(App.Vector(sx * 19.23, sy * 11.385, 0), App.Vector(0, 0, 1), 1.85), False)
    i = lead.addConstraint(Sketcher.Constraint("DistanceX", -1, 1, k, 3, sx * 19.23))
    lead.renameConstraint(i, "X%d" % k)
    lead.setExpression("Constraints.X%d" % k, ("" if sx > 0 else "-") + P + "ScrewX")
    i = lead.addConstraint(Sketcher.Constraint("DistanceY", -1, 1, k, 3, sy * 11.385))
    lead.renameConstraint(i, "Y%d" % k)
    lead.setExpression("Constraints.Y%d" % k, ("" if sy > 0 else "-") + P + "ScrewY")
    if k == 0:
        i = lead.addConstraint(Sketcher.Constraint("Diameter", 0, 3.7))
        lead.renameConstraint(i, "LeadInDiameter")
        lead.setExpression("Constraints.LeadInDiameter", P + "InsertLeadInDiameter")
    else:
        lead.addConstraint(Sketcher.Constraint("Equal", 0, k))
lead.Visibility = False
leadin = body.newObject("PartDesign::Pocket", "InsertLeadInBore")
leadin.Profile = lead
leadin.setExpression("Length", P + "InsertLeadInDepth")
leadin.Refine = True
body.Tip = leadin
for o in body.Group:
    if o is not leadin and hasattr(o, "Visibility") and o.TypeId.startswith("PartDesign::") and o.TypeId != "PartDesign::SubShapeBinder":
        o.Visibility = False
leadin.Visibility = True

doc.recompute()

log = open(RESULT, "w")
log.write("bad: %s\n" % [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State])
log.write("outline fully=%s  holes fully=%s placement=%s  lead fully=%s\n" % (
    outline.FullyConstrained, holes.FullyConstrained, holes.Placement, lead.FullyConstrained))
for gi in (2, 3, 8, 9, 22):
    g = outline.Geometry[gi]
    log.write("  outline geo%d (%.3f,%.3f)-(%.3f,%.3f)\n" % (gi, g.StartPoint.x, g.StartPoint.y, g.EndPoint.x, g.EndPoint.y))
for gi in range(4):
    g = holes.Geometry[gi]
    log.write("  grip circle %d c=(%.3f,%.3f) r=%.3f\n" % (gi, g.Center.x, g.Center.y, g.Radius))
tip = body.Tip.Shape.copy()
tip.Placement = App.Placement()
log.write("tip %s volume=%.3f solids=%d valid=%s\n" % (body.Tip.Name, tip.Volume, len(tip.Solids), tip.isValid()))
lid = doc.getObject("Body003")
ls = lid.Shape.copy()
ls.transformShape(body.Placement.inverse().toMatrix())
log.write("lid tip=%s volume=%.3f bb=%s\n" % (lid.Tip.Name, ls.Volume, ls.BoundBox))
log.write("lid hole sketch fully=%s placement=%s\n" % (lidholes.FullyConstrained, lidholes.Placement))
for k in range(4):
    g = lidholes.Geometry[k]
    log.write("  lid hole sketch circle %d c=(%.3f,%.3f) r=%.3f\n" % (k, g.Center.x, g.Center.y, g.Radius))
for n in ("Pocket029", "Pocket030", "Chamfer001"):
    log.write("  lid feature %s volume=%.3f\n" % (n, doc.getObject(n).Shape.Volume))
# is the lid open (air) at each screw centre and solid 0.6 mm outside the clearance hole?
zmid = (ls.BoundBox.ZMin + ls.BoundBox.ZMax) / 2
for sx in (1, -1):
    for sy in (1, -1):
        x, y = sx * vs.ScrewX.Value, sy * vs.ScrewY.Value
        log.write("  lid at (%.2f,%.2f): centre solid=%s  ring(+1.6mm) solid=%s\n" % (
            x, y, ls.isInside(App.Vector(x, y, zmid), 1e-4, True),
            all(ls.isInside(App.Vector(x + 1.6 * dx, y + 1.6 * dy, zmid), 1e-4, True)
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))))
log.close()
