"""Build an approximate mechanical model of the E Ink ET011TJ1 module.

The model is based on the mechanical drawing in ET011TJ1 Rev. 1.0. Units are
millimetres. Critical envelope, active-area, mounting-hole, flex, and connector
dimensions are represented; tiny undimensioned moulding details are simplified.

Run with FreeCADCmd:
    FreeCADCmd build_et011tj1.py
"""

from pathlib import Path
import math

import FreeCAD as App
import Part
import Mesh


OUT = Path(__file__).resolve().parent
DOC = App.newDocument("ET011TJ1")


def color(rgb):
    return tuple(c / 255.0 for c in rgb)


def add_feature(name, label, shape, rgb):
    obj = DOC.addObject("PartDesign::Feature", name)
    obj.Label = label
    obj.Shape = shape
    obj.addProperty("App::PropertyString", "Source", "Documentation")
    obj.Source = "ET011TJ1 Rev. 1.0 mechanical drawing"
    # FreeCADCmd has no GUI ViewObject; the property is set when available.
    if obj.ViewObject is not None:
        obj.ViewObject.ShapeColor = color(rgb)
    return obj


def centred_box(x_len, y_len, z_len, z0, x=0.0, y=0.0):
    return Part.makeBox(
        x_len, y_len, z_len,
        App.Vector(x - x_len / 2.0, y - y_len / 2.0, z0),
    )


def octagonal_pad(width, height, chamfer, z_len, z0, x, y):
    hx, hy, c = width / 2.0, height / 2.0, chamfer
    pts = [
        (-hx + c, -hy), (hx - c, -hy), (hx, -hy + c), (hx, hy - c),
        (hx - c, hy), (-hx + c, hy), (-hx, hy - c), (-hx, -hy + c),
    ]
    wire = Part.makePolygon(
        [App.Vector(x + px, y + py, z0) for px, py in pts]
        + [App.Vector(x + pts[0][0], y + pts[0][1], z0)]
    )
    return Part.Face(wire).extrude(App.Vector(0, 0, z_len))


def ring(outer_d, inner_d, height, z0, x=0.0, y=0.0):
    outer = Part.makeCylinder(outer_d / 2.0, height, App.Vector(x, y, z0))
    inner = Part.makeCylinder(inner_d / 2.0, height, App.Vector(x, y, z0))
    return outer.cut(inner)


# Drawing-controlled headline dimensions.
OUTLINE_X = 36.4
OUTLINE_Y = 52.8
OUTLINE_Z = 5.1
ACTIVE_D = 27.96
MOUNT_X = 25.0
MOUNT_Y = 26.0
MOUNT_HOLE_D = 1.5

# Plastic back case and the moulded bridges at 12 and 6 o'clock.
body = Part.makeCylinder(17.7, 2.95, App.Vector(0, 0, 0.45))
body = body.fuse(centred_box(21.0, 6.0, 2.95, 0.45, y=16.0))
body = body.fuse(centred_box(22.0, 5.0, 2.95, 0.45, y=-15.2))

# Four mounting ears. Holes run through the complete model stack.
boss_centres = [
    (-MOUNT_X / 2, MOUNT_Y / 2), (MOUNT_X / 2, MOUNT_Y / 2),
    (-MOUNT_X / 2, -MOUNT_Y / 2), (MOUNT_X / 2, -MOUNT_Y / 2),
]
for bx, by in boss_centres:
    pad = octagonal_pad(5.2, 5.2, 1.05, 2.95, 0.45, bx, by)
    body = body.fuse(pad)
    hole = Part.makeCylinder(MOUNT_HOLE_D / 2, OUTLINE_Z, App.Vector(bx, by, 0))
    body = body.cut(hole)

housing = add_feature("Housing", "Plastic back case", body, (55, 60, 65))

# E-paper/front stack. The outer ring is raised around the circular active area.
carrier = Part.makeCylinder(16.87, 0.42, App.Vector(0, 0, 0.10))
carrier = carrier.fuse(ring(17.35 * 2, 29.2, 0.45, 0.0))
for bx, by in boss_centres:
    carrier = carrier.cut(Part.makeCylinder(MOUNT_HOLE_D / 2, 0.52, App.Vector(bx, by, 0)))
bezel = add_feature("Bezel", "Front carrier and bezel", carrier, (28, 31, 34))

active = Part.makeCylinder(ACTIVE_D / 2.0, 0.10, App.Vector(0, 0, 0))
display = add_feature("DisplaySurface", "E-paper active area - diameter 27.96 mm", active, (218, 216, 203))

# Rear mounting bosses extend the model to the specified 5.1 mm maximum depth.
boss_shape = None
for bx, by in boss_centres:
    pad = octagonal_pad(4.7, 4.7, 0.9, 1.70, 3.40, bx, by)
    collar = ring(4.0, MOUNT_HOLE_D, 1.70, 3.40, bx, by)
    one = pad.fuse(collar).cut(
        Part.makeCylinder(MOUNT_HOLE_D / 2.0, 1.70, App.Vector(bx, by, 3.40))
    )
    boss_shape = one if boss_shape is None else boss_shape.fuse(one)
bosses = add_feature("MountBosses", "Four rear mounting bosses", boss_shape, (78, 83, 87))

# Six small side clips seen in the orthographic views. Their profiles are
# simplified because no complete moulding dimensions are supplied.
clips_shape = None
clip_data = [
    (-17.2, 6.0, 2.0, 2.2), (-17.2, -5.0, 2.0, 2.2),
    (17.2, 6.0, 2.0, 2.2), (17.2, -5.0, 2.0, 2.2),
    (-7.0, -17.2, 2.0, 1.0), (7.0, -17.2, 2.0, 1.0),
]
for cx, cy, sx, sy in clip_data:
    c = centred_box(sx, sy, 1.15, 2.25, cx, cy)
    clips_shape = c if clips_shape is None else clips_shape.fuse(c)
clips = add_feature("RetainingClips", "Simplified retaining clips", clips_shape, (94, 99, 103))

# Flexible tail: 7.2 mm wide, reaching the connector and preserving the exact
# 52.8 mm overall Y envelope (-17.7 to +35.1 mm).
flex = centred_box(7.2, 15.5, 0.16, 3.40, y=25.45)
flex = flex.fuse(centred_box(16.8, 3.0, 0.16, 3.40, y=18.0))
flex_obj = add_feature("FlexTail", "Flexible printed tail", flex, (180, 111, 48))

# Panasonic AXT624124 connector envelope. Contact fingers are modelled as
# raised features for visual and assembly clarity.
connector_body = centred_box(7.6, 2.3, 1.54, 3.56, y=33.95)
connector_obj = add_feature("Connector", "Panasonic AXT624124 connector", connector_body, (38, 41, 44))

contacts_shape = None
for i in range(12):
    x = -2.75 + i * 0.5
    pin = centred_box(0.20, 1.75, 0.08, 5.02, x=x, y=33.95)
    contacts_shape = pin if contacts_shape is None else contacts_shape.fuse(pin)
contacts = add_feature("ConnectorContacts", "Connector contact fingers", contacts_shape, (198, 158, 56))

# Rear barcode label and a few shallow ribs make orientation obvious in CAD.
label_shape = centred_box(13.0, 8.0, 0.04, 3.40, y=-2.0)
barcode_label = add_feature("BarcodeLabel", "Rear barcode label", label_shape, (218, 218, 212))

bars_shape = None
for i, w in enumerate([0.22, 0.45, 0.22, 0.30, 0.55, 0.22, 0.38, 0.22, 0.45]):
    x = -4.8 + i * 1.15
    bar = centred_box(w, 5.6, 0.025, 3.44, x=x, y=-2.0)
    bars_shape = bar if bars_shape is None else bars_shape.fuse(bar)
barcode = add_feature("Barcode", "Representative barcode ribs", bars_shape, (35, 35, 35))

# Store key dimensions inside the FreeCAD document.
spec = DOC.addObject("App::FeaturePython", "Specifications")
spec.Label = "Drawing dimensions (mm)"
for name, value in [
    ("OverallWidth", OUTLINE_X), ("OverallLength", OUTLINE_Y),
    ("MaximumThickness", OUTLINE_Z), ("ActiveAreaDiameter", ACTIVE_D),
    ("MountPitchX", MOUNT_X), ("MountPitchY", MOUNT_Y),
    ("MountHoleDiameter", MOUNT_HOLE_D),
]:
    spec.addProperty("App::PropertyLength", name, "ET011TJ1")
    setattr(spec, name, value)

DOC.recompute()

objects = [housing, bezel, display, bosses, clips, flex_obj, connector_obj, contacts, barcode_label, barcode]
compound = Part.makeCompound([o.Shape for o in objects])
export_obj = DOC.addObject("PartDesign::Feature", "ExportCompound")
export_obj.Label = "Complete ET011TJ1 module"
export_obj.Shape = compound
if export_obj.ViewObject is not None:
    export_obj.ViewObject.Visibility = False

# The CAD files retain separately selectable components. The STL is fused into
# one printable shell so slicers do not have to infer unions between bodies.
print_shape = objects[0].Shape
for obj in objects[1:]:
    print_shape = print_shape.fuse(obj.Shape)
print_shape = print_shape.removeSplitter()
print_obj = DOC.addObject("PartDesign::Feature", "PrintSolid")
print_obj.Label = "Fused printable ET011TJ1 module"
print_obj.Shape = print_shape
if print_obj.ViewObject is not None:
    print_obj.ViewObject.Visibility = False
DOC.recompute()

DOC.saveAs(str(OUT / "ET011TJ1.FCStd"))
Part.export(objects, str(OUT / "ET011TJ1.step"))
Mesh.export([print_obj], str(OUT / "ET011TJ1.stl"))

bb = compound.BoundBox
print(
    "ET011TJ1 envelope: "
    f"{bb.XLength:.3f} x {bb.YLength:.3f} x {bb.ZLength:.3f} mm; "
    f"bounds X[{bb.XMin:.3f},{bb.XMax:.3f}] "
    f"Y[{bb.YMin:.3f},{bb.YMax:.3f}] Z[{bb.ZMin:.3f},{bb.ZMax:.3f}]"
)
print(f"Printable solid count: {len(print_shape.Solids)}")
print("Wrote ET011TJ1.FCStd, ET011TJ1.step, and ET011TJ1.stl")
