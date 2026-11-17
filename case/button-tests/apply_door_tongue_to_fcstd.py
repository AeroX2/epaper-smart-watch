"""Add the door-tongue buttons to a case FCStd as editable PartDesign features.

Set FCSTD (path) and RESULT (log path) in the globals before exec()ing this file.
The old hole/button/strip features are suppressed, not deleted. Everything is driven
by the "Door tongue" group in the ButtonParameters VarSet and by ButtonLayout.

Sketch frames follow the existing button sketches:
    wall sketches   x = along the wall (t), y = up from ButtonHeight (body z)
    pad sketch      x = outward (n),        y = up from ButtonHeight
"""
import FreeCAD as App
import Part
import Sketcher

V = App.Vector
P = "ButtonParameters."
HC = "(%sRimHeight - %sButtonHeight)" % (P, P)  # rim height above the button centre

PARAMS = [  # name, value, description
    ("RimHeight", 8.08, "Body z of the open rim (sits on the bed)"),
    ("DoorFreeEdge", 2.0, "Button centre to tongue free edge"),
    ("DoorLength", 6.0, "Button centre to hinge; longer = softer and lower stress"),
    ("DoorHeight", 3.7, "Tongue height from the rim"),
    ("DoorSlotWidth", 0.40, "Vertical slot at the free edge"),
    ("DoorTopGap", 0.55, "Bridged slot above the tongue"),
    ("DoorCutDepth", 6.0, "Slot cut depth, centred on the switch tip plane"),
    ("NubHalfWidth", 1.0, "Actuator nub half width"),
    ("NubGap", 0.05, "Rest gap between nub and switch tip"),
    ("NubLength", 0.85, "Nub length from its tip into the wall"),
    ("NubAboveCentre", 0.8, "Nub end past the button centre, away from the rim"),
    ("PadHalfWidth", 1.6, "Outer pad half width"),
    ("PadOut", 2.1, "Pad face distance from the switch tip plane"),
    ("PadRoot", 1.35, "Pad root distance (buried in the wall)"),
    ("PadRimSide", 1.6, "Pad start from button centre, rim side"),
    ("PadFarSide", 1.2, "Pad end from button centre, away from the rim"),
]
OLD = ["LeftHolePocket", "LeftButtonPad", "LeftStripPad", "RightHolePocket", "RightButtonPad",
       "RightStripPad", "MirrorButtonsAndHoles", "AllButtonFillets"]

doc = App.openDocument(FCSTD)
body = doc.getObject("Body")
vs = doc.getObject("ButtonParameters")
old_mirror = doc.getObject("MirrorButtonsAndHoles")
group = doc.getObject("ButtonDesign")

for name, value, text in PARAMS:
    vs.addProperty("App::PropertyLength", name, "Door tongue", text)
    setattr(vs, name, value)


def new_sketch(name, label, placement):
    s = body.newObject("Sketcher::SketchObject", name)
    s.Label = label
    s.MapMode = "Deactivated"
    s.setExpression("Placement", placement)
    s.Visibility = False
    return s


def dim(s, kind, geo, pos, value, name, expr):
    """Signed distance from the sketch origin to a point, driven by an expression."""
    i = s.addConstraint(Sketcher.Constraint(kind, -1, 1, geo, pos, value))
    s.renameConstraint(i, name)
    s.setExpression("Constraints." + name, expr)


def polygon(s, pts):
    n = len(pts)
    s.addGeometry([Part.LineSegment(V(*pts[i], 0), V(*pts[(i + 1) % n], 0)) for i in range(n)], False)
    s.addConstraint([Sketcher.Constraint("Coincident", i, 2, (i + 1) % n, 1) for i in range(n)])


def slot_sketch(side, sign, frame):
    s = new_sketch(side + "DoorSlotSketch", side + "DoorSlotSketch", frame["wall"])
    hc, top, gap, W, S, L = 2.185, 3.7, 0.55, 2.0, 0.40, 6.0
    r = gap / 2
    tf, tf2, tc = -sign * W, -sign * (W + S), sign * (L - r)
    y_rim, y_t, y_g = hc + 1, hc - top, hc - top - gap
    yc = (y_t + y_g) / 2
    circle = Part.Circle(V(tc, yc, 0), V(0, 0, 1), r)
    import math
    arc = Part.ArcOfCircle(circle, -math.pi / 2, math.pi / 2) if sign > 0 else \
        Part.ArcOfCircle(circle, math.pi / 2, 3 * math.pi / 2)
    s.addGeometry([
        Part.LineSegment(V(tf2, y_rim, 0), V(tf, y_rim, 0)),   # 0 past the rim
        Part.LineSegment(V(tf, y_rim, 0), V(tf, y_t, 0)),      # 1 free edge
        Part.LineSegment(V(tf, y_t, 0), V(tc, y_t, 0)),        # 2 tongue top
        arc,                                                   # 3 round hinge end
        Part.LineSegment(V(tc, y_g, 0), V(tf2, y_g, 0)),       # 4 bridged edge
        Part.LineSegment(V(tf2, y_g, 0), V(tf2, y_rim, 0)),    # 5 slot outer edge
    ], False)
    # arc runs from its y_g end to its y_t end when sign > 0, the reverse otherwise
    at_t, at_g = (2, 1) if sign > 0 else (1, 2)
    s.addConstraint([
        Sketcher.Constraint("Coincident", 0, 2, 1, 1),
        Sketcher.Constraint("Coincident", 1, 2, 2, 1),
        Sketcher.Constraint("Tangent", 2, 2, 3, at_t),
        Sketcher.Constraint("Tangent", 3, at_g, 4, 1),
        Sketcher.Constraint("Coincident", 4, 2, 5, 1),
        Sketcher.Constraint("Coincident", 5, 2, 0, 1),
        Sketcher.Constraint("Horizontal", 0),
        Sketcher.Constraint("Horizontal", 2),
        Sketcher.Constraint("Horizontal", 4),
        Sketcher.Constraint("Vertical", 1),
        Sketcher.Constraint("Vertical", 5),
    ])
    sg = "" if sign > 0 else "-"     # sign of the hinge side
    fg = "-" if sign > 0 else ""     # sign of the free side
    dim(s, "DistanceX", 1, 1, tf, "FreeEdge", "%s%sDoorFreeEdge" % (fg, P))
    dim(s, "DistanceX", 0, 1, tf2, "SlotOuter", "%s(%sDoorFreeEdge + %sDoorSlotWidth)" % (fg, P, P))
    dim(s, "DistanceY", 0, 1, y_rim, "PastRim", "%s + 1 mm" % HC)
    dim(s, "DistanceY", 1, 2, y_t, "TongueTop", "%s - %sDoorHeight" % (HC, P))
    dim(s, "DistanceY", 4, 1, y_g, "TopGapEdge", "%s - %sDoorHeight - %sDoorTopGap" % (HC, P, P))
    dim(s, "DistanceX", 3, 3, tc, "HingeCentre", "%s(%sDoorLength - %sDoorTopGap / 2)" % (sg, P, P))
    return s


def nub_sketch(side, frame):
    s = new_sketch(side + "NubSketch", side + "NubSketch", frame["nub"])
    w, hc, a = 1.0, 2.185, 0.8
    polygon(s, [(-w, -a), (w, -a), (w, hc), (-w, hc)])
    s.addConstraint([Sketcher.Constraint("Horizontal", 0), Sketcher.Constraint("Vertical", 1),
                     Sketcher.Constraint("Horizontal", 2), Sketcher.Constraint("Vertical", 3)])
    dim(s, "DistanceX", 0, 1, -w, "Left", "-%sNubHalfWidth" % P)
    dim(s, "DistanceX", 0, 2, w, "Right", "%sNubHalfWidth" % P)
    dim(s, "DistanceY", 0, 1, -a, "FarEnd", "-%sNubAboveCentre" % P)
    dim(s, "DistanceY", 2, 1, hc, "RimEnd", HC)
    return s


def pad_sketch(side, frame):
    s = new_sketch(side + "PadSketch", side + "PadSketch", frame["pad"])
    polygon(s, [(1.35, 1.6), (2.1, 0.85), (2.1, -1.2), (1.35, -1.2)])
    s.addConstraint([Sketcher.Constraint("Vertical", 1), Sketcher.Constraint("Horizontal", 2),
                     Sketcher.Constraint("Vertical", 3)])
    dim(s, "DistanceX", 0, 1, 1.35, "Root", P + "PadRoot")
    dim(s, "DistanceY", 0, 1, 1.6, "RimSide", P + "PadRimSide")
    dim(s, "DistanceX", 1, 1, 2.1, "Face", P + "PadOut")
    dim(s, "DistanceY", 1, 1, 0.85, "ChamferEnd", "%sPadRimSide - (%sPadOut - %sPadRoot)" % (P, P, P))
    dim(s, "DistanceY", 2, 1, -1.2, "FarSide", "-%sPadFarSide" % P)
    return s


def frames(side):
    x, y = "ButtonLayout.Constraints.%sX" % side, "ButtonLayout.Constraints.%sY" % side
    a = "ButtonLayout.Constraints.%sAngle" % side
    z = P + "ButtonHeight"
    g = P + "NubGap"
    return {
        "wall": "placement(vector(%s; %s; %s); rotation(%s; 0; 90))" % (x, y, z, a),
        "nub": "placement(vector(%s + %s * sin(%s); %s - %s * cos(%s); %s); rotation(%s; 0; 90))" % (
            x, g, a, y, g, a, z, a),
        "pad": "placement(vector(%s; %s; %s); rotation(%s - 90 deg; 0; 90))" % (x, y, z, a),
    }


def symmetric(feature):
    feature.Midplane = True
    if hasattr(feature, "SideType"):
        feature.SideType = "Symmetric"


for name in OLD:
    doc.getObject(name).Suppressed = True

originals = []
for side, sign in (("Left", -1), ("Right", 1)):
    fr = frames(side)
    pocket = body.newObject("PartDesign::Pocket", side + "DoorSlot")
    pocket.Profile = slot_sketch(side, sign, fr)
    symmetric(pocket)
    pocket.setExpression("Length", P + "DoorCutDepth")
    pocket.Refine = True

    nub = body.newObject("PartDesign::Pad", side + "ActuatorNub")
    nub.Profile = nub_sketch(side, fr)
    nub.setExpression("Length", P + "NubLength")
    nub.Refine = True

    pad = body.newObject("PartDesign::Pad", side + "FingerPad")
    pad.Profile = pad_sketch(side, fr)
    symmetric(pad)
    pad.setExpression("Length", "2 * %sPadHalfWidth" % P)
    pad.Refine = True
    originals += [pocket, nub, pad]

mirror = body.newObject("PartDesign::Mirrored", "MirrorDoorTongues")
mirror.Label = "Mirror door tongues to the other edge"
mirror.Originals = originals
mirror.MirrorPlane = old_mirror.MirrorPlane
mirror.TransformMode = old_mirror.TransformMode
mirror.Refine = True
body.Tip = mirror
doc.getObject("AllButtonFillets").Visibility = False
for o in originals:
    o.Visibility = False
mirror.Visibility = True

doc.recompute()

log = open(RESULT, "w")
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
log.write("bad objects: %s\n" % bad)
for o in doc.Objects:
    if o.TypeId == "Sketcher::SketchObject" and ("Door" in o.Name or "Nub" in o.Name or "PadSketch" in o.Name):
        log.write("%s solve=%s fully=%s placement=%s\n" % (o.Name, o.solve(), o.FullyConstrained, o.Placement))
for o in originals + [mirror]:
    log.write("%s volume=%.3f\n" % (o.Name, o.Shape.Volume))
tip = body.Tip.Shape
log.write("tip %s solids=%d valid=%s volume=%.3f\n" % (body.Tip.Name, len(tip.Solids), tip.isValid(), tip.Volume))
lid = doc.getObject("Body003").Shape
log.write("lid volume=%.3f bb=%s\n" % (lid.Volume, lid.BoundBox))
area = doc.getObject("Area")
log.write("area label=%s\n" % area.Label)
if "COMPARE" in globals():
    ref = Part.read(COMPARE)
    local = tip.copy()
    local.Placement = App.Placement()
    log.write("ref volume=%.3f  tip-ref=%.4f  ref-tip=%.4f\n" % (
        ref.Volume, local.cut(ref).Volume, ref.cut(local).Volume))
log.close()
