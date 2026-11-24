"""Rework a copy of case.FCStd into the sculpted case with swept ends, by editing its own steps.

    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" -c "exec(open(r'case/sculpted/build_sculpted_fcstd.py').read())"

Writes case/sculpted/case-swept-v30.FCStd (case.FCStd itself is not changed).

Existing steps that are changed
    Sketch001 (base outline)   wall moved out 1 mm (R20 -> R21, flanks offset), old lug tabs removed
    Sketch006 (deck boss)      was a R18.5 circle; now the whole outline, so the deck is at lip height everywhere
    ButtonParameters.PadOut    +1 mm, so the finger pads come through the thicker wall
    Lid                        v13 edits by hand (six-edge chamfer, centred plug, side chamfer), then LidBowl (one curve from the case wall to the caseback, all round), LidLugRoots (the two ends as blocks, flush with the case's flanks), LidLugSweep (the underside curve, shared with the cheeks), LidFan x4 (the bowl's own curve turned round each flank's start, so the bowl runs into the lug with no wall), LidScoop (the hollow between the lugs: wall curving into floor), LidLugRound and LidGapRound (rounded lower edges), LidLugWells and LidLugHoles for the screws
    sketch attachments         switched to fixed placements (they pointed at face numbers that move)

Existing steps that are deleted (with their sketches, where nothing else uses them)
    old button design (8 steps), Pocket016, Pocket033, Pocket022, SubtractivePipe, SubtractivePipe001, Chamfer

New steps on the body
    (no bottom bevel: the wall runs straight down to the rim and the lid's LidBowl carries the curve on to the caseback)
    SculptButtonWindow    pocket    groove on the inside of the wall at the hinge end of each door tongue
    SculptCeilingStep/Drop pad      lowers the cavity ceiling in two steps towards the ends, under the falling top
    SculptEndShell        pad       far end: curved shell, one sweep from the end wall down to where the band comes out
    SculptCheekA / B      pad       side cheeks at both ends, carrying the spring bars
    SculptEndTrim         pocket    cuts the ends to the flank lines and rounds their four corners in plan
    SculptTop             loft cut  the top: circular land, 27 deg fall across the wrist, 14 deg along the strap
    SculptShoulder...     fillet    rounding where the top meets the sides
    SculptBarHoles        pocket    spring bar holes
    SculptFeet            pocket    small flats under the four cheeks, for printing cheeks-down
    SculptFlapRails       pad       an upright rib on each cheek's inner face at the USB end: the flap slides down onto them
Separate bodies
    UsbFlap                         the same curved shell at the USB end. It slides down between the cheeks: grooves in its
                                    sides run on the cheeks' ribs (they hold it against pulling off), and a sprung catch on
                                    its back clicks under the top edge of the port opening (it holds it down)

The glass pocket (Pocket, R15.35 x 1.2) and the window (Pocket021) are untouched.

Body coordinates: x = X, y = -Y, z = -Z (rim at z = +8.08, lip at z = -1.0).
Side-view sketches use x = X and y = height (global Z).
"""

import json
import math
import os
import shutil

import FreeCAD as App
import MeshPart
import Part
import Sketcher  # noqa: F401

HERE = r"C:\Users\James\Documents\git\epaper-smart-watch\case\sculpted"
SRC = os.path.join(os.path.dirname(HERE), "case.FCStd")
NAME = "case-swept-v30"
DST = os.path.join(HERE, NAME + ".FCStd")
LOG = open(os.path.join(HERE, "report.txt"), "w")
V = App.Vector


def log(*a):
    LOG.write(" ".join(str(x) for x in a) + "\n")
    LOG.flush()


# ---------------------------------------------------------------- parameters
GROW = 1.0           # wall moved out by this much
R_OLD = 20.0
END_X = 23.0
OLD_FLANK = ((26.10, 13.30), (13.46, 14.80))   # flank line in the old base sketch
TOP_Z = 1.0          # top land = lip height (glass top is at 0.7)
LAND_R = 17.4        # circular flat land round the glass
SLOPE_Y = 0.51       # fall of the top per mm, across the wrist (27 deg)
SLOPE_X = 0.2536     # fall along the strap (14 deg): any steeper and the top meets the display at the cavity ends
TOP_BOTTOM_Z = -6.0
RIM_Z = -8.08
BOTTOM_BEVEL = 0.8   # 1.0 does not build; leaves 1.0 of rim for the lid to sit on
LID_CHAMFER = 0.95
CEIL_STEP_X, CEIL_STEP = 17.9, 0.4    # ceiling -0.5 -> -0.9 from here out (display front face is at -1.1)
CEIL_DROP_X, CEIL_DROP = 19.3, 0.85   # ceiling -0.5 -> -1.35 from here out (display there is at -1.56 or lower)
WINDOW_TOP_Z = -3.6   # hinge groove: from the rim up to here, just above the door slot (-3.83)
WINDOW_MARGIN = 0.7   # groove starts this far past the hinge line, clear of the round slot end
GROOVE_WIDTH = 4.0    # and runs this far along the tongue
HINGE_THICKNESS = 1.0 # wall left at the groove; must exceed the bottom bevel
GAP = 10.0            # half the gap between the cheeks (20 mm band)
BAR_X, BAR_Z, BAR_R = 25.4, -11.3, 0.6   # spring bars: under the USB plug (plug body is -2.0 .. -8.5)
NOSE_R = 2.2          # cheek end is a circle round the bar: 1.6 mm of wall round the hole
END_ROOT_X = 22.6     # the end pieces start here, inside the end wall
HOOD_X = 28.8         # front of the swept ends (band wraps the bar out to X = 28.0)
SHELL_T = 2.4         # shell thickness at the centre line (the top cut thins it to about 1.6 at the cheeks)
SKIRT_Z = -8.3        # the sweep ends here, just above the band (band top is near -8.7)
TOP_RAISE = 0.3       # the ends are built this much proud of the top and cut back by SculptTop
FEET_CUT = 0.5        # cut off the bottom of each cheek nose
CORNER_R = 3.5        # plan-view radius of the four corners of the ends
SHOULDER_RADII = (1.5, 1.4, 1.2, 0.9)
CREST_FILLET = 0.4
FLAP_GAP = 0.2        # clearance round the USB flap
FLAP_BLOCK_X = 25.9   # the flap is solid behind its shell out to here
FLAP_BASE_Z = -7.8    # and down to here (the band's top is near -8.7)
RAIL_X0, RAIL_W = 24.0, 1.0   # the cheeks' ribs: where they start along X and how thick they are
RAIL_H = 0.8          # how far they stand out from the cheek's inner face
RAIL_TOP_Z = -3.75    # their top; the flap's roof over the grooves is 1 mm thick there
RAIL_FIT = 0.12       # clearance each side of a rib in its groove
PORT_TOP_Z = -3.27    # top edge of the port opening (flat across Y +-3), in the end wall X 22.18 .. 23.0
CATCH_REACH = 0.4     # how far the catch reaches under that edge
SPRING_T = 0.6        # the catch sits on a strip this thick,
SPRING_H = 1.8        # this tall,
SPRING_HALF = 8.0     # and this long each way from the middle; it is held at both ends and bows in the middle
SWEEP_START_X = 19.3  # the sweep leaves the top here, 3.7 mm inside the end wall
SWEEP_PROUD = 0.3     # the end pieces are built this much proud of the sweep and cut back by SculptEndSweep
# The underside of each lug is one surface shared by the lid and the case: level with the raised caseback,
# then one curve down to the foot of the cheek. The lid carries it from the caseback out to its end (the
# "lug roots"), the cheeks carry it on from there; side faces are flush with the case's flanks and cheeks.
# (The lid itself: a 1 mm plate, Z -8.08..-9.08, chamfered all round, with a round raised caseback R19.39
# standing 1.5 mm proud at Z = -10.58, flat out to R18.19.)
UNDER_X0 = 17.0       # the underside curve leaves the caseback level here
LUG_ROOT_R = 18.0     # the lug roots start inside the flat of the raised caseback, so they run out of it flush
SCOOP_R = 4.0         # plan radius of the scoop between the lugs, where it runs on from each cheek's inner face
SCOOP_BLEND = 2.0     # the scoop's wall curves into its floor (the lid's plate) with this radius

assert HINGE_THICKNESS >= BOTTOM_BEVEL + 0.2
assert BAR_X - NOSE_R >= END_X + 0.15

(_x1, _y1), (_x2, _y2) = OLD_FLANK
_len = math.hypot(_x2 - _x1, _y2 - _y1)
_nx, _ny = (_y1 - _y2) / _len, (_x1 - _x2) / _len
FLANK_M = (_y2 - _y1) / (_x2 - _x1)
FLANK_X0, FLANK_Y0 = _x1 + _nx * GROW, _y1 + _ny * GROW
R_NEW = R_OLD + GROW


def flank_y(x):
    return FLANK_Y0 + FLANK_M * (x - FLANK_X0)


def top_semi(z):
    """Half axes (along X, along Y) of the top surface's outline at height z."""
    return LAND_R + (TOP_Z - z) / SLOPE_X, LAND_R + (TOP_Z - z) / SLOPE_Y


def top_at(x, y):
    """Height of the top surface over the point (x, y)."""
    if math.hypot(x, y) <= LAND_R:
        return TOP_Z
    lo, hi = -40.0, TOP_Z
    for _ in range(50):
        mid = (lo + hi) / 2
        a, b = top_semi(mid)
        if (x / a) ** 2 + (y / b) ** 2 > 1:
            hi = mid
        else:
            lo = mid
    return (lo + hi) / 2


# ---------------------------------------------------------------- open the copy
shutil.copyfile(SRC, DST)
doc = App.openDocument(DST)
body = doc.getObject("Body")
lid = doc.getObject("Body003")
old_shape = body.Shape.copy()
log("start: volume %.1f" % old_shape.Volume)

for o in list(body.Group) + list(lid.Group):
    if o.TypeId == "Sketcher::SketchObject" and o.MapMode != "Deactivated":
        pl = App.Placement(o.Placement)
        o.MapMode = "Deactivated"
        o.Placement = pl
doc.recompute()
log("after freeing sketch attachments: volume %.1f (must equal start)" % body.Shape.Volume)


def outline_geometry():
    a = 1 + FLANK_M ** 2
    b = 2 * FLANK_M * (FLANK_Y0 - FLANK_M * FLANK_X0)
    c = (FLANK_Y0 - FLANK_M * FLANK_X0) ** 2 - R_NEW ** 2
    xa = (-b + math.sqrt(b * b - 4 * a * c)) / (2 * a)
    ya, ye = flank_y(xa), flank_y(END_X)
    a0 = math.atan2(ya, xa)
    big = Part.Circle(V(0, 0, 0), V(0, 0, 1), R_NEW)
    return [
        Part.ArcOfCircle(big, a0, math.pi - a0),
        Part.LineSegment(V(-xa, ya, 0), V(-END_X, ye, 0)),
        Part.LineSegment(V(-END_X, ye, 0), V(-END_X, -ye, 0)),
        Part.LineSegment(V(-END_X, -ye, 0), V(-xa, -ya, 0)),
        Part.ArcOfCircle(big, math.pi + a0, 2 * math.pi - a0),
        Part.LineSegment(V(xa, -ya, 0), V(END_X, -ye, 0)),
        Part.LineSegment(V(END_X, -ye, 0), V(END_X, ye, 0)),
        Part.LineSegment(V(END_X, ye, 0), V(xa, ya, 0)),
    ]


def replace_geometry(sk, geo):
    sk.deleteAllConstraints()
    sk.deleteAllGeometry()
    sk.addGeometry(geo, False)


# ---------------------------------------------------------------- delete the replaced steps
orphans = []
for name in ("AllButtonFillets", "MirrorButtonsAndHoles", "RightStripPad", "RightButtonPad", "RightHolePocket",
             "LeftStripPad", "LeftButtonPad", "LeftHolePocket",
             "Pocket016", "Pocket033", "Pocket022", "SubtractivePipe", "SubtractivePipe001", "Chamfer"):
    f = doc.getObject(name)
    for prop in ("Profile", "Spine", "AuxillerySpine"):
        v = getattr(f, prop, None)
        o = v[0] if isinstance(v, tuple) else v
        if o is not None and o.TypeId == "Sketcher::SketchObject" and o not in orphans:
            orphans.append(o)
    body.removeObject(f)
    doc.removeObject(name)
doc.recompute()
for o in orphans:
    if not [u for u in o.InList if u is not body and u.TypeId != "App::DocumentObjectGroup"]:
        body.removeObject(o)
        doc.removeObject(o.Name)

# ---------------------------------------------------------------- edit the existing steps
replace_geometry(doc.getObject("Sketch"), outline_geometry())
replace_geometry(doc.getObject("Sketch005"), outline_geometry())
vs = doc.getObject("ButtonParameters")
vs.PadOut = vs.PadOut.Value + GROW
doc.getObject("Chamfer001").Size = LID_CHAMFER
# lid edits James made by hand in case-sculpted-v13.FCStd (2026-11-21)
doc.getObject("Chamfer001").Base = (doc.getObject("Pad006"), ["Edge4", "Edge22", "Edge19", "Edge16", "Edge10", "Edge7"])
replace_geometry(doc.getObject("Sketch068"), [Part.Circle(V(0, 0, 0), V(0, 0, 1), 19.39)])
doc.recompute()
p30 = doc.getObject("Pocket030")
lid_ch = doc.addObject("PartDesign::Chamfer", "LidSideChamfer")
lid.insertObject(lid_ch, p30, True)
lid_ch.Base = (p30, ["Edge25", "Edge23", "Edge27", "Edge41", "Edge42", "Edge43"])
lid_ch.Size = 0.5
lid_ch.Refine = True
doc.recompute()
doc.getObject("Fillet").Base = (doc.getObject("Pad007"), ["Edge88"])
doc.recompute()
log("lid Pocket032 volume %.1f (in your v13: 1540.4)" % doc.getObject("Pocket032").Shape.Volume)
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
log("after editing base steps: volume %.1f, errors %s" % (body.Shape.Volume, bad))


# ---------------------------------------------------------------- the underside curve (lid and cheeks share it)
_p32 = doc.getObject("Pocket032").Shape.BoundBox          # lid coordinates: z = -Z - 0.5
LID_IN_Z = -_p32.ZMin - 0.5                              # the lid's inner face (the case rim), -8.08
LID_BOSS_Z = -_p32.ZMax - 0.5                            # the raised caseback, -10.58
FOOT_Z = BAR_Z - NOSE_R + FEET_CUT                       # the flats under the cheeks
# the curve runs into the nose circle a hair below the flat, so the flat's cut crosses it cleanly
_jz = FOOT_Z - 0.06
UNDER_JOIN = (BAR_X - math.sqrt(NOSE_R ** 2 - (BAR_Z - _jz) ** 2), _jz)
_d, _drop = UNDER_JOIN[0] - UNDER_X0, LID_BOSS_Z - _jz
UNDER_R = (_d * _d + _drop * _drop) / (2 * _drop)        # circle level at (UNDER_X0, caseback), centre below
log("underside curve: level with the caseback (Z %.2f) to X=%.1f, then R %.1f down to the cheek foot at (%.2f, %.2f)" % (
    LID_BOSS_Z, UNDER_X0, UNDER_R, UNDER_JOIN[0], UNDER_JOIN[1]))


def under_z(x):
    x = abs(x)
    if x <= UNDER_X0:
        return LID_BOSS_Z
    return LID_BOSS_Z - UNDER_R + math.sqrt(UNDER_R ** 2 - (x - UNDER_X0) ** 2)


# ---------------------------------------------------------------- helpers
def sketch(name, placement, owner=None):
    s = (owner or body).newObject("Sketcher::SketchObject", name)
    s.MapMode = "Deactivated"
    s.Placement = placement
    s.Visibility = False
    return s


def plan_at(body_z):
    return App.Placement(V(0, 0, body_z), App.Rotation())


def side_at(body_y):
    # sketch x = body x, sketch y = height (global Z), normal = +body y
    return App.Placement(V(0, body_y, 0), App.Rotation(V(1, 0, 0), -90))


def rect(s, x0, y0, x1, y1):
    p = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
    s.addGeometry([Part.LineSegment(p[i], p[(i + 1) % 4]) for i in range(4)], False)


def poly(s, pts):
    n = len(pts)
    s.addGeometry([Part.LineSegment(V(pts[i][0], pts[i][1], 0), V(pts[(i + 1) % n][0], pts[(i + 1) % n][1], 0))
                   for i in range(n)], False)


def report(feature, before):
    sh = feature.Shape
    ok = feature.isValid() and not sh.isNull()
    log("%-22s valid=%s solids=%d shells=%d volume %+.1f" % (
        feature.Name, ok and sh.isValid(), len(sh.Solids) if ok else 0, len(sh.Shells) if ok else 0,
        (sh.Volume - before) if ok else 0.0))
    return sh.Volume if ok else before


def add(feature, expect):
    """Recompute a new sketch-based step; if it changed the volume the wrong way, turn it round."""
    before = add.volume
    feature.Refine = True
    doc.recompute()
    delta = feature.Shape.Volume - before
    if expect and hasattr(feature, "Reversed") and delta * expect <= 1e-6:
        feature.Reversed = not feature.Reversed
        doc.recompute()
    add.volume = report(feature, before)


def edge_names(shape, test):
    return ["Edge%d" % (i + 1) for i, e in enumerate(shape.Edges) if test(e)]


def dressup(kind, name, base, names, prop, values, insert_after=None):
    """Fillet or chamfer on named edges of `base`; tries each value until one gives a sound solid."""
    if not names:
        log("%-22s no edges found" % name)
        return None
    for val in values:
        f = doc.addObject(kind, name)
        if insert_after is None:
            body.addObject(f)
        else:
            body.insertObject(f, insert_after, True)
        f.Base = (base, names)
        setattr(f, prop, val)
        f.Refine = True
        doc.recompute()
        sh = f.Shape
        sound = f.isValid() and not sh.isNull() and sh.isValid() and len(sh.Solids) == 1
        if sound:
            try:
                # a fillet can pass isValid() and still break the next step, so try a cut through each end
                for sx in (1, -1):
                    probe = sh.cut(Part.makeBox(4, 60, 6, V(sx * BAR_X - 2, -30, 9)))
                    sound = sound and probe.isValid() and len(probe.Solids) == 1
            except Exception:
                sound = False
        if sound and sh.Volume < base.Shape.Volume:
            log("%-22s %s=%.2f on %d edges, volume %+.2f" % (name, prop, val, len(names), sh.Volume - base.Shape.Volume))
            return f
        log("%-22s %s=%.2f on %d edges did not build" % (name, prop, val, len(names)))
        body.removeObject(f)
        doc.removeObject(f.Name)
        doc.recompute()
    return None


# ---------------------------------------------------------------- bottom bevel, right after the rim cut
rim_cut = doc.getObject("Pocket027")
rim_z = -RIM_Z


def rim_outer_edge(e):
    b = e.BoundBox
    if abs(b.ZMin - rim_z) > 1e-3 or abs(b.ZMax - rim_z) > 1e-3:
        return False
    for p in e.discretize(4):
        on_arc = abs(math.hypot(p.x, p.y) - R_NEW) < 0.01
        # not the end walls: a bevel there cuts the 0.8 mm strip under the USB port to a knife edge
        # not the flanks: there the wall runs straight down into the lid's lug roots
        if not on_arc:
            return False
    return True


# No bevel on the case any more: the wall runs straight down to the rim, and the lid (LidBowl, lug roots)
# carries the surface on from there, so case and lid read as one piece.

# ---------------------------------------------------------------- hinge grooves (inside face), after the button steps
anchor = doc.getObject("InsertLeadInBore")
window_sketch = doc.addObject("Sketcher::SketchObject", "SculptButtonWindowSketch")
body.insertObject(window_sketch, anchor, False)
window_sketch.MapMode = "Deactivated"
window_sketch.Placement = plan_at(-WINDOW_TOP_Z)
window_sketch.Visibility = False
for slot_name in ("LeftDoorSlotSketch", "RightDoorSlotSketch"):
    sk = doc.getObject(slot_name)
    hinge = [g.Center.x for g in sk.Geometry if hasattr(g, "Radius")][0]
    away = -1.0 if hinge > 0 else 1.0
    t0, t1 = sorted((hinge - away * WINDOW_MARGIN, hinge + away * GROOVE_WIDTH))
    c = sk.Placement.Base
    tdir = sk.Placement.Rotation.multVec(V(1, 0, 0))
    angs = sorted(math.atan2(c.y + tdir.y * t, c.x + tdir.x * t) for t in (t0, t1))
    for mirror in (1, -1):
        a0, a1 = (angs[0], angs[1]) if mirror > 0 else (-angs[1], -angs[0])
        ri, ro = 19.0, R_NEW - HINGE_THICKNESS
        window_sketch.addGeometry([
            Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), ri), a0, a1),
            Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), ro), a0, a1),
            Part.LineSegment(V(ri * math.cos(a0), ri * math.sin(a0), 0), V(ro * math.cos(a0), ro * math.sin(a0), 0)),
            Part.LineSegment(V(ri * math.cos(a1), ri * math.sin(a1), 0), V(ro * math.cos(a1), ro * math.sin(a1), 0)),
        ], False)
window = doc.addObject("PartDesign::Pocket", "SculptButtonWindow")
body.insertObject(window, anchor, False)
window.Profile = window_sketch
window.Type = 1
window.Refine = True
doc.recompute()
above = Part.makeBox(80, 80, 20, V(-40, -40, -WINDOW_TOP_Z - 20.001))


def cut_above():
    return window.BaseFeature.Shape.common(above).Volume - window.Shape.common(above).Volume


if cut_above() > 1e-3:      # through-all removes material either way: it must go down to the rim, not up
    window.Reversed = not window.Reversed
    doc.recompute()
log("%-22s valid=%s volume %+.1f, removed above the tongue zone %.3f (must be 0)" % (
    "SculptButtonWindow", window.Shape.isValid(),
    window.Shape.Volume - window.BaseFeature.Shape.Volume, cut_above()))
body.Tip = doc.getObject("InsertLeadInBore")
doc.recompute()
add.volume = body.Tip.Shape.Volume
log("body before the new end steps: volume %.1f, shells %d" % (add.volume, len(body.Tip.Shape.Shells)))

# ---------------------------------------------------------------- cavity ceiling, stepped down towards the ends
for name, x0, depth in (("SculptCeilingStep", CEIL_STEP_X, CEIL_STEP), ("SculptCeilingDrop", CEIL_DROP_X, CEIL_DROP)):
    s = sketch(name + "Sketch", plan_at(0.5))
    rect(s, x0, -8.9, 22.4, 8.9)
    rect(s, -21.8, -8.9, -x0, 8.9)
    f = body.newObject("PartDesign::Pad", name)
    f.Profile = s
    f.Length = depth
    add(f, +1)

# ---------------------------------------------------------------- the swept ends
_nrm = math.hypot(SLOPE_X, 1)
_n = (SLOPE_X / _nrm, 1 / _nrm)                     # upward normal of the top line in side view


def top_line(x):
    return TOP_Z - (x - LAND_R) * SLOPE_X + TOP_RAISE


def true_top(x):
    return TOP_Z - (x - LAND_R) * SLOPE_X


# One long sweep: a circle tangent to the top at X = SWEEP_START_X (inside the end wall, on the case itself)
# that comes down to the skirt edge at (HOOD_X, SKIRT_Z). Its radius follows from those two conditions.
_tx, _tz = SWEEP_START_X, true_top(SWEEP_START_X)
_fx, _fz = HOOD_X, SKIRT_Z
# centre = T - R*n ; |centre - F| = R  ->  R = |T-F|^2 / (2 * (T-F).n)
_dxf, _dzf = _tx - _fx, _tz - _fz
SWEEP_R = (_dxf * _dxf + _dzf * _dzf) / (2 * (_dxf * _n[0] + _dzf * _n[1]))
SWEEP_CX, SWEEP_CZ = _tx - SWEEP_R * _n[0], _tz - SWEEP_R * _n[1]
SWEEP_A_END = math.atan2(_fz - SWEEP_CZ, _fx - SWEEP_CX)
log("sweep: radius %.2f mm, from X=%.1f down to (%.1f, %.1f)" % (SWEEP_R, SWEEP_START_X, HOOD_X, SKIRT_Z))


def sweep_angle(x, proud=0.0):
    """Angle on the sweep circle (upper side) over a given X."""
    return math.acos(min(1.0, (x - SWEEP_CX) / (SWEEP_R + proud)))


def sweep_pt(r, a):
    return (SWEEP_CX + r * math.cos(a), SWEEP_CZ + r * math.sin(a))


def sweep_inner_z(x):
    ri = SWEEP_R - SHELL_T
    return SWEEP_CZ + math.sqrt(max(0.0, ri * ri - (x - SWEEP_CX) ** 2))


def outer_curve(sign, root_x, proud=0.0):
    """The sweep from root_x down to the skirt level: shared by the shell, the cheeks and the flap.
    Returns the arc and its two end points. With proud > 0 it stands outside the true sweep."""
    r = SWEEP_R + proud
    a0, a1 = sweep_angle(root_x, proud), math.asin((SKIRT_Z - SWEEP_CZ) / r)
    p0, pm, p1 = sweep_pt(r, a0), sweep_pt(r, (a0 + a1) / 2), sweep_pt(r, a1)
    return [Part.ArcOfCircle(V(sign * p0[0], p0[1], 0), V(sign * pm[0], pm[1], 0), V(sign * p1[0], p1[1], 0))], p0, p1


def shell_profile(s, sign, root_x, proud=0.0):
    def P(pt):
        return V(sign * pt[0], pt[1], 0)
    ri = SWEEP_R - SHELL_T
    a_g = math.asin((SKIRT_Z - SWEEP_CZ) / ri)
    # the inner arc starts over the same X as the outer one, so the shell's back edge is vertical
    a0i = math.acos(min(1.0, (root_x - SWEEP_CX) / ri))
    g, gm, gi = sweep_pt(ri, a_g), sweep_pt(ri, (a0i + a_g) / 2), sweep_pt(ri, a0i)
    arc, p0, p1 = outer_curve(sign, root_x, proud)
    s.addGeometry(arc + [
        Part.LineSegment(P(p1), P(g)),
        Part.ArcOfCircle(P(g), P(gm), P(gi)),
        Part.LineSegment(P(gi), P(p0)),
    ], False)


def cheek_profile(s, sign):
    def P(x, z):
        return V(sign * x, z, 0)
    # from the skirt edge down a tangent to the nose circle, round under the bar, and up the back
    dx, dz = HOOD_X - BAR_X, SKIRT_Z - BAR_Z
    ang = math.atan2(dz, dx) - math.acos(NOSE_R / math.hypot(dx, dz))
    n1 = (BAR_X + NOSE_R * math.cos(ang), BAR_Z + NOSE_R * math.sin(ang))
    back = BAR_X - NOSE_R
    arc, top0, lip = outer_curve(sign, END_ROOT_X, SWEEP_PROUD)
    s.addGeometry(arc + [
        Part.LineSegment(P(*lip), P(HOOD_X, SKIRT_Z)),
        Part.LineSegment(P(HOOD_X, SKIRT_Z), P(*n1)),
        # round under the bar, then back up the underside curve that the lid's lug roots bring out to here
        Part.ArcOfCircle(P(*n1), P(BAR_X, BAR_Z - NOSE_R), P(*UNDER_JOIN)),
        Part.ArcOfCircle(P(*UNDER_JOIN), P((UNDER_JOIN[0] + back) / 2, under_z((UNDER_JOIN[0] + back) / 2)),
                         P(back, under_z(back))),
        Part.LineSegment(P(back, under_z(back)), P(back, RIM_Z + 0.2)),
        Part.LineSegment(P(back, RIM_Z + 0.2), P(END_ROOT_X, RIM_Z + 0.2)),
        Part.LineSegment(P(END_ROOT_X, RIM_Z + 0.2), P(*top0)),
    ], False)


# round corners in plan, where each flank meets the front of the end
_fn = math.hypot(FLANK_M, 1)
CORNER_N = (-FLANK_M / _fn, 1 / _fn)                       # outward normal of the flank line
CORNER_CX = HOOD_X - CORNER_R
CORNER_CY = flank_y(CORNER_CX) - CORNER_R * _fn            # centre: CORNER_R inside both the flank and the front


def on_corner(p, tol=0.03):
    return abs(p.x) > CORNER_CX - 0.01 and abs(math.hypot(abs(p.x) - CORNER_CX, abs(p.y) - CORNER_CY) - CORNER_R) < tol


half_w = flank_y(END_ROOT_X) + 0.6
s = sketch("SculptEndShellSketch", side_at(0))
shell_profile(s, -1, END_ROOT_X, SWEEP_PROUD)                   # far end only; the USB end's shell is the flap
f = body.newObject("PartDesign::Pad", "SculptEndShell")
f.Profile = s
f.SideType = "Symmetric"
f.Length = 2 * half_w
add(f, +1)

for tag, y in (("A", GAP), ("B", -GAP)):
    s = sketch("SculptCheekSketch" + tag, side_at(y))
    cheek_profile(s, +1)
    cheek_profile(s, -1)
    f = body.newObject("PartDesign::Pad", "SculptCheek" + tag)
    f.Profile = s
    f.Length = half_w - GAP
    f.Reversed = (tag == "B")
    add(f, +1)

s = sketch("SculptEndTrimSketch", plan_at(0))
for sx in (1, -1):
    for sy in (1, -1):
        # outside the flank line, and outside a round corner where the flank meets the front
        x0, x1 = END_ROOT_X - 0.6, HOOD_X + 2
        tf = (CORNER_CX + CORNER_R * CORNER_N[0], CORNER_CY + CORNER_R * CORNER_N[1])
        am = (math.atan2(CORNER_N[1], CORNER_N[0])) / 2
        mid = (CORNER_CX + CORNER_R * math.cos(am), CORNER_CY + CORNER_R * math.sin(am))

        def Q(x, y):
            return V(sx * x, sy * y, 0)

        s.addGeometry([
            Part.LineSegment(Q(x0, flank_y(x0)), Q(*tf)),
            Part.ArcOfCircle(Q(*tf), Q(*mid), Q(HOOD_X, CORNER_CY)),
            Part.LineSegment(Q(HOOD_X, CORNER_CY), Q(x1, CORNER_CY)),
            Part.LineSegment(Q(x1, CORNER_CY), Q(x1, 22)),
            Part.LineSegment(Q(x1, 22), Q(x0, 22)),
            Part.LineSegment(Q(x0, 22), Q(x0, flank_y(x0))),
        ], False)
f = body.newObject("PartDesign::Pocket", "SculptEndTrim")
f.Profile = s
f.Type = 1
f.SideType = "Symmetric"
add(f, -1)


# ---------------------------------------------------------------- the top
def top_sections(owner, prefix, globalz):
    out = []
    # from the land (an exact circle at the land height) down to an ellipse: nothing stands above the land
    for i, z in enumerate((TOP_Z, TOP_BOTTOM_Z)):
        sk = sketch("%sSketch%d" % (prefix, i + 1),
                    App.Placement(V(0, 0, z), App.Rotation()) if globalz else plan_at(-z), owner)
        rect(sk, -70, -50, 70, 50)      # must stay outside the ellipse (45 x 31 at the bottom)
        a, b = top_semi(z)
        # both sections must be ellipses for the loft to treat them as the same hole; the top one is
        # a circle to within 0.0001 mm
        sk.addGeometry(Part.Ellipse(V(0, 0, 0), max(a, b + 1e-4), b), False)
        out.append(sk)
    return out


cut = body.newObject("PartDesign::SubtractiveLoft", "SculptTop")
secs = top_sections(body, "SculptTop", False)
cut.Profile = secs[0]
cut.Sections = [secs[1]]
cut.Ruled = True
add(cut, 0)

# The sweep itself: one cut across the whole width at each end, from where it leaves the top (on the case,
# inside the end wall) down to the skirt edge. The end pieces were built proud of it, so the case and the
# end pieces share one surface with no seam between them.
s = sketch("SculptEndSweepSketch", side_at(0))
for sx in (1, -1):
    a0 = sweep_angle(SWEEP_START_X)
    t0, tm = sweep_pt(SWEEP_R, a0), sweep_pt(SWEEP_R, (a0 + SWEEP_A_END) / 2)
    xo, zt, zb = HOOD_X + 2, TOP_Z + 2, SKIRT_Z - 1

    def W(x, z):
        return V(sx * x, z, 0)

    s.addGeometry([
        Part.ArcOfCircle(W(*t0), W(*tm), W(HOOD_X, SKIRT_Z)),
        Part.LineSegment(W(HOOD_X, SKIRT_Z), W(HOOD_X, zb)),
        Part.LineSegment(W(HOOD_X, zb), W(xo, zb)),
        Part.LineSegment(W(xo, zb), W(xo, zt)),
        Part.LineSegment(W(xo, zt), W(t0[0], zt)),
        Part.LineSegment(W(t0[0], zt), W(*t0)),
    ], False)
f = body.newObject("PartDesign::Pocket", "SculptEndSweep")
f.Profile = s
f.Type = 1
f.SideType = "Symmetric"
add(f, -1)


# ---------------------------------------------------------------- rounding
def on_top(p, tol=0.03):
    return abs(-p.z - top_at(p.x, p.y)) < tol


def on_flank_plane(p, tol=0.02):
    return abs(abs(p.y) - flank_y(abs(p.x))) < tol


def shoulder_edge(e):
    if e.Length < 0.3 or e.BoundBox.ZMin < -TOP_Z + 0.05:
        return False
    pts = e.discretize(6)
    return all(on_top(p) for p in pts) and \
        all(abs(math.hypot(p.x, p.y) - R_NEW) < 0.02 or on_flank_plane(p) for p in pts)


def cheek_side_edge(e):
    if e.Length < 0.3:
        return False
    b = e.BoundBox
    if b.XLength + b.YLength < 0.05:        # a vertical seam where the round corner runs into the flank
        return False
    pts = e.discretize(6)
    return all((on_flank_plane(p) or on_corner(p)) and abs(p.x) > END_X + 0.02 for p in pts) \
        and not all(on_top(p) for p in pts)


def arc_shoulder_edge(e):
    return shoulder_edge(e) and all(abs(math.hypot(p.x, p.y) - R_NEW) < 0.02 for p in e.discretize(6))


tip = body.Tip
both = dressup("PartDesign::Fillet", "SculptShoulderAndCheeks", tip,
               edge_names(tip.Shape, lambda e: shoulder_edge(e) or cheek_side_edge(e)), "Radius", SHOULDER_RADII)
if not both:
    tip = body.Tip
    if not dressup("PartDesign::Fillet", "SculptShoulder", tip, edge_names(tip.Shape, shoulder_edge),
                   "Radius", SHOULDER_RADII):
        dressup("PartDesign::Fillet", "SculptShoulder", tip, edge_names(tip.Shape, arc_shoulder_edge),
                "Radius", SHOULDER_RADII)
    tip = body.Tip
    dressup("PartDesign::Fillet", "SculptCheekEdges", tip, edge_names(tip.Shape, cheek_side_edge),
            "Radius", SHOULDER_RADII)
tip = body.Tip
dressup("PartDesign::Fillet", "SculptCrest", tip,
        edge_names(tip.Shape, lambda e: abs(e.BoundBox.ZMin + TOP_Z) < 1e-4 and abs(e.BoundBox.ZMax + TOP_Z) < 1e-4
                   and min(math.hypot(v.X, v.Y) for v in e.Vertexes) > LAND_R - 0.1), "Radius", (CREST_FILLET, 0.3))
add.volume = body.Tip.Shape.Volume

# ---------------------------------------------------------------- bar holes and feet
s = sketch("SculptBarHolesSketch", side_at(0))
for sx in (1, -1):
    s.addGeometry(Part.Circle(V(sx * BAR_X, BAR_Z, 0), V(0, 0, 1), BAR_R), False)
f = body.newObject("PartDesign::Pocket", "SculptBarHoles")
f.Profile = s
f.SideType = "Two sides"
f.Length = GAP + 0.5
f.Length2 = GAP + 0.5
add(f, -1)

feet_z = FOOT_Z
s = sketch("SculptFeetSketch", plan_at(-feet_z))
rect(s, -45, -35, 45, 35)
f = body.newObject("PartDesign::Pocket", "SculptFeet")
f.Profile = s
f.Type = 1
f.Refine = True
before = add.volume
doc.recompute()
if before - f.Shape.Volume > 30:        # only the four nose tips may go
    f.Reversed = not f.Reversed
    doc.recompute()
log("%-22s valid=%s volume %+.2f (four small tips only)" % ("SculptFeet", f.Shape.isValid(), f.Shape.Volume - before))

# An upright rib on the inner face of each cheek at the USB end. The flap's grooves slide down onto them.
# Front view sketch on the plane X = RAIL_X0: sketch x = body y, sketch y = body z (= -Z). The underside is
# cut at 45 degrees so it prints without support, and the top has a lead-in for the flap.
_front = App.Matrix()
_front.A11, _front.A21, _front.A31 = 0, 1, 0
_front.A12, _front.A22, _front.A32 = 0, 0, 1
_front.A13, _front.A23, _front.A33 = 1, 0, 0
s = sketch("SculptFlapRailsSketch", App.Placement(V(RAIL_X0, 0, 0), App.Rotation(_front)))
_tip, _in = GAP - RAIL_H, GAP + 0.3                      # the rib's face, and 0.3 inside the cheek so it joins
for sy in (1, -1):
    poly(s, [(sy * _in, -RAIL_TOP_Z), (sy * (_tip + 0.3), -RAIL_TOP_Z), (sy * _tip, -(RAIL_TOP_Z - 0.3)),
             (sy * _tip, -(FLAP_BASE_Z + RAIL_H)), (sy * _in, -(FLAP_BASE_Z - 0.3))])
f = body.newObject("PartDesign::Pad", "SculptFlapRails")
f.Profile = s
f.Length = RAIL_W
f.Refine = True
doc.recompute()
_want = Part.makeBox(RAIL_W - 0.02, RAIL_H - 0.02, 2.0, V(RAIL_X0 + 0.01, GAP - RAIL_H + 0.01, 5.0))   # body z = -Z
if f.Shape.common(_want).Volume < 0.9 * _want.Volume:
    f.Reversed = not f.Reversed
    doc.recompute()
assert f.Shape.isValid() and len(f.Shape.Solids) == 1 and f.Shape.common(_want).Volume > 0.9 * _want.Volume
log("%-22s valid=%s solids=%d volume %+.2f (two ribs, %.1f x %.1f, Z %.2f .. %.2f)" % (
    "SculptFlapRails", f.Shape.isValid(), len(f.Shape.Solids), f.Shape.Volume - doc.getObject("SculptFeet").Shape.Volume,
    RAIL_W, RAIL_H, FLAP_BASE_Z, RAIL_TOP_Z))

# ---------------------------------------------------------------- lid: finished step is Pocket032
lid.Tip = doc.getObject("Pocket032")
doc.recompute()

# ---------------------------------------------------------------- lid: lug roots
# The lid and the case read as one piece: at each corner the lid carries a lug root whose outer face is the
# case's flank carried straight down, whose inner face runs on from the cheek's inner face and flares in
# to the caseback, and whose underside is the shared curve (level with the caseback, then down to the foot).
# Lid coordinates: x = X, y = -Y, z = -Z - 0.5, so a side-view sketch has y = global Z + 0.5.
LID_SCREW_X, LID_SCREW_Y, LID_RECESS_R, LID_HOLE_R, LID_RECESS_FLOOR = 19.23, 11.38, 1.6, 1.05, 0.5


def gcyl(r, x, y, z0, z1):
    return Part.makeCylinder(r, z1 - z0, V(x, y, z0))


def screw_columns(shape):
    """(material in the screw heads' way, material in the screw shafts' way, material round the shafts)."""
    head = shaft = seat = 0.0
    for sx in (1, -1):
        for sy in (1, -1):
            x, y = sx * LID_SCREW_X, sy * LID_SCREW_Y
            floor = LID_IN_Z - LID_RECESS_FLOOR
            head += shape.common(gcyl(LID_RECESS_R - 0.03, x, y, floor - 8, floor - 0.01)).Volume
            shaft += shape.common(gcyl(LID_HOLE_R - 0.03, x, y, floor - 0.01, LID_IN_Z + 0.5)).Volume
            seat += shape.common(gcyl(LID_RECESS_R - 0.03, x, y, floor + 0.01, LID_IN_Z - 0.01)).Volume
    return head, shaft, seat


old_lid = lid.Shape.copy()
lid_before = old_lid.Volume
seat_before = screw_columns(old_lid)[2]
# the hollow under the raised caseback (the battery's room) must stay as it is
hollow = gcyl(17.1, 0, 0, LID_BOSS_Z + 0.85, LID_IN_Z + 0.2)
hollow_before = old_lid.common(hollow).Volume

# the bowl: one curve from the case wall at the rim (R21, vertical there) to the caseback's flat (level
# there), revolved all round. It replaces the stepped bevel / plate edge / caseback edge.
BOWL_R0 = LUG_ROOT_R                                     # level point, just inside the caseback's flat (R18.19)
LUG_SPLIT_R = 19.45                                      # just outside the raised caseback (R19.39)
LUG_SHELL_Z = LID_BOSS_Z + 0.8                           # the floor of the hollow under the caseback


def bowl_z(r):
    if r <= BOWL_R0:
        return LID_BOSS_Z
    if r >= R_NEW:
        return LID_IN_Z
    return LID_IN_Z - (LID_IN_Z - LID_BOSS_Z) * math.sqrt(1 - ((r - BOWL_R0) / (R_NEW - BOWL_R0)) ** 2)


s = sketch("LidBowlSketch", side_at(0), lid)
_ell = Part.Ellipse(V(R_NEW, LID_IN_Z + 0.5, 0), V(BOWL_R0, LID_BOSS_Z + 0.5, 0), V(BOWL_R0, LID_IN_Z + 0.5, 0))
_p0, _p1 = V(BOWL_R0, LID_BOSS_Z + 0.5, 0), V(R_NEW, LID_IN_Z + 0.5, 0)
_u0, _u1 = _ell.parameter(_p0), _ell.parameter(_p1)
_arc = None
for _a, _b in ((_u0, _u1), (_u1, _u0), (_u0, _u1 + 2 * math.pi), (_u1, _u0 + 2 * math.pi)):
    if _b <= _a:
        continue
    _try = Part.ArcOfEllipse(_ell, _a, _b)
    _m = _try.value((_a + _b) / 2)
    if _b - _a < math.pi and _m.x > BOWL_R0 and _m.y < LID_IN_Z + 0.5:
        _arc = _try
assert _arc is not None
_step_z = LUG_SHELL_Z - 0.12 + 0.5                       # inside the caseback's shell, below the hollow's floor
s.addGeometry([
    _arc,
    Part.LineSegment(_p0, V(BOWL_R0, _step_z, 0)),
    Part.LineSegment(V(BOWL_R0, _step_z, 0), V(LUG_SPLIT_R, _step_z, 0)),
    Part.LineSegment(V(LUG_SPLIT_R, _step_z, 0), V(LUG_SPLIT_R, LID_IN_Z + 0.5, 0)),
    Part.LineSegment(V(LUG_SPLIT_R, LID_IN_Z + 0.5, 0), _p1),
], False)
f = lid.newObject("PartDesign::Revolution", "LidBowl")
f.Profile = s
f.ReferenceAxis = (s, ["V_Axis"])
f.Angle = 360
f.Refine = True
lid.Tip = f
doc.recompute()
log("LidBowl                valid=%s solids=%d volume %+.1f" % (
    f.Shape.isValid(), len(f.Shape.Solids), f.Shape.Volume - lid_before))
assert f.Shape.isValid() and f.Shape.Volume > lid_before + 50
lid_before_roots = f.Shape.Volume

# where the flank leaves the round part of the case
_a = 1 + FLANK_M ** 2
_b = 2 * FLANK_M * (FLANK_Y0 - FLANK_M * FLANK_X0)
_c = (FLANK_Y0 - FLANK_M * FLANK_X0) ** 2 - R_NEW ** 2
ROOT_X = (-_b + math.sqrt(_b * _b - 4 * _a * _c)) / (2 * _a)
ROOT_Y = flank_y(ROOT_X)
_ap = math.atan2(ROOT_Y, ROOT_X)
BOWL_W = R_NEW - BOWL_R0                                 # the bowl's curve is this wide (and rim-to-caseback tall)
SCOOP_Y = GAP - SCOOP_R                                  # the scoop's two round ends turn about (END_X, +-SCOOP_Y)
SCOOP_FLOOR_Z = LID_IN_Z - 1.0                           # the underside of the lid's plate
_wall = math.hypot(LID_SCREW_X - END_X, LID_SCREW_Y - SCOOP_Y) - SCOOP_R - LID_RECESS_R
log("lug roots: from the flank's start (%.2f, %.2f) to the lid's end; the scoop between them leaves %.2f mm of wall inside each screw well" % (
    ROOT_X, ROOT_Y, _wall))
assert _wall >= 0.6 and UNDER_X0 >= ROOT_X + BOWL_W


def polar(r, a):
    return (r * math.cos(a), r * math.sin(a))


def arc3(Q, p0, pm, p1):
    return Part.ArcOfCircle(Q(*p0), Q(*pm), Q(*p1))


def ring_arc(Q, r, a0, a1):
    return arc3(Q, polar(r, a0), polar(r, (a0 + a1) / 2), polar(r, a1))


def end_pad(name, z_from, outline):
    """One block at each end of the lid, from z_from down past the feet (LidLugSweep cuts the underside)."""
    global _v
    sk = sketch(name + "Sketch", App.Placement(V(0, 0, -z_from - 0.5), App.Rotation()), lid)
    for sx in (1, -1):
        def Q(x, y):
            return V(sx * x, y, 0)

        sk.addGeometry(outline(Q), False)
    f = lid.newObject("PartDesign::Pad", name)
    f.Profile = sk
    f.Length = z_from - FOOT_Z + 0.6
    f.Refine = True
    lid.Tip = f
    doc.recompute()
    if f.Shape.BoundBox.ZMax < -FOOT_Z - 0.5:           # went up into the case instead of down
        f.Reversed = not f.Reversed
        doc.recompute()
    log("%-22s valid=%s solids=%d volume %+.1f" % (name, f.Shape.isValid(), len(f.Shape.Solids), f.Shape.Volume - _v))
    _v = f.Shape.Volume


# Each end of the lid is first one plain block between the two flanks, from the radial line through each
# flank's start out to the lid's end. No walls are left standing: every face of the block that would show
# is cut to a curve by the steps that follow. (Padded in two parts so the hollow under the caseback stays.)
_v = lid_before_roots
end_pad("LidLugRoots", LID_IN_Z, lambda Q: [
    ring_arc(Q, LUG_SPLIT_R, -_ap, _ap),                                        # round the raised caseback
    Part.LineSegment(Q(*polar(LUG_SPLIT_R, _ap)), Q(ROOT_X, ROOT_Y)),           # out to the flank's start
    Part.LineSegment(Q(ROOT_X, ROOT_Y), Q(END_X, flank_y(END_X))),              # the case's flank
    Part.LineSegment(Q(END_X, flank_y(END_X)), Q(END_X, -flank_y(END_X))),      # the lid's end
    Part.LineSegment(Q(END_X, -flank_y(END_X)), Q(ROOT_X, -ROOT_Y)),
    Part.LineSegment(Q(ROOT_X, -ROOT_Y), Q(*polar(LUG_SPLIT_R, -_ap))),
])
_ri, _ro = 17.2, LUG_SPLIT_R + 0.15
end_pad("LidLugJoin", LUG_SHELL_Z, lambda Q: [
    ring_arc(Q, _ro, -_ap, _ap),
    Part.LineSegment(Q(*polar(_ro, _ap)), Q(*polar(_ri, _ap))),
    ring_arc(Q, _ri, _ap, -_ap),
    Part.LineSegment(Q(*polar(_ri, -_ap)), Q(*polar(_ro, -_ap))),
])

# the underside: everything below the shared curve goes (nothing of the lid was below the caseback before)
s = sketch("LidLugSweepSketch", side_at(0), lid)
for sx in (1, -1):
    x0, x1, zb = 11.0, END_X + 0.5, FOOT_Z - 1.5
    xm = (UNDER_X0 + x1) / 2
    s.addGeometry([
        Part.LineSegment(V(sx * x0, LID_BOSS_Z + 0.5, 0), V(sx * UNDER_X0, LID_BOSS_Z + 0.5, 0)),
        Part.ArcOfCircle(V(sx * UNDER_X0, LID_BOSS_Z + 0.5, 0), V(sx * xm, under_z(xm) + 0.5, 0),
                         V(sx * x1, under_z(x1) + 0.5, 0)),
        Part.LineSegment(V(sx * x1, under_z(x1) + 0.5, 0), V(sx * x1, zb + 0.5, 0)),
        Part.LineSegment(V(sx * x1, zb + 0.5, 0), V(sx * x0, zb + 0.5, 0)),
        Part.LineSegment(V(sx * x0, zb + 0.5, 0), V(sx * x0, LID_BOSS_Z + 0.5, 0)),
    ], False)
_v = lid.Shape.Volume
f = lid.newObject("PartDesign::Pocket", "LidLugSweep")
f.Profile = s
f.Type = 1
f.SideType = "Symmetric"
f.Refine = True
lid.Tip = f
doc.recompute()
log("LidLugSweep            valid=%s solids=%d volume %+.1f" % (
    f.Shape.isValid(), len(f.Shape.Solids), f.Shape.Volume - _v))
_v = f.Shape.Volume


def cut_step(kind, name, sk, **props):
    """A subtractive step; returns the volume it removed."""
    global _v
    f = lid.newObject(kind, name)
    f.Profile = sk
    for k, val in props.items():
        setattr(f, k, val)
    f.Refine = True
    lid.Tip = f
    doc.recompute()
    sh = f.Shape
    assert f.isValid() and sh.isValid() and len(sh.Solids) == 1, name
    cut, _v = _v - sh.Volume, sh.Volume
    return cut


# The fans: at each flank's start the bowl's own curve is turned about the upright line through that corner.
# In the radial plane through the corner it is the bowl's section exactly, so the two surfaces are tangent
# there; a quarter turn or so later it lies in the flank's plane, 3 mm along the flank, and arrives level
# with the caseback. So the bowl dies away into the lug's underside and the flank's face grows out of the
# corner along the same curve. There is no wall and no crease between the round part and the lug root.
_zb = FOOT_Z - 1.5 + 0.5
FAN_CORE = 0.02
_removed = 0.0
_n = 0
for sx in (1, -1):
    for sy in (1, -1):
        _n += 1
        sk = sketch("LidFanSketch%d" % _n, App.Placement(V(sx * ROOT_X, sy * ROOT_Y, 0), App.Rotation(V(1, 0, 0), -90)), lid)
        _ell = Part.Ellipse(V(0, LID_IN_Z + 0.5, 0), V(BOWL_W, LID_BOSS_Z + 0.5, 0), V(BOWL_W, LID_IN_Z + 0.5, 0))
        # The curve stops FAN_CORE short of the corner line and a 0.02 mm core carries the cut up through
        # the rim there: a profile that touches its own axis upright makes a singular tip, and the cut
        # then silently removes nothing.
        _q0 = V(BOWL_W, LID_BOSS_Z + 0.5, 0)
        _q1 = V(FAN_CORE, LID_IN_Z + 0.5 - (LID_IN_Z - LID_BOSS_Z) * math.sqrt(1 - ((BOWL_W - FAN_CORE) / BOWL_W) ** 2), 0)
        _u0, _u1 = _ell.parameter(_q0), _ell.parameter(_q1)
        _arc = None
        for _a, _b in ((_u0, _u1), (_u1, _u0), (_u0, _u1 + 2 * math.pi), (_u1, _u0 + 2 * math.pi)):
            if _b <= _a:
                continue
            _try = Part.ArcOfEllipse(_ell, _a, _b)
            _m = _try.value((_a + _b) / 2)
            if _b - _a < math.pi and _m.x < BOWL_W and _m.y < LID_IN_Z + 0.5:
                _arc = _try
        assert _arc is not None
        sk.addGeometry([
            _arc,
            Part.LineSegment(_q0, V(BOWL_W, _zb, 0)),
            Part.LineSegment(V(BOWL_W, _zb, 0), V(0, _zb, 0)),
            Part.LineSegment(V(0, _zb, 0), V(0, LID_IN_Z + 1.5, 0)),
            Part.LineSegment(V(0, LID_IN_Z + 1.5, 0), V(FAN_CORE, LID_IN_Z + 1.5, 0)),
            Part.LineSegment(V(FAN_CORE, LID_IN_Z + 1.5, 0), _q1),
        ], False)
        _cut = cut_step("PartDesign::Groove", "LidFan%d" % _n, sk, ReferenceAxis=(sk, ["V_Axis"]), Angle=360)
        assert 2.0 < _cut < 2.7, ("fan cut", _n, _cut)
        _removed += _cut
log("LidFan1..4             valid, 1 solid, volume %+.2f (2.35 each when the cut takes)" % -_removed)

# The scoop between the two lugs at each end: its wall runs on from the cheek's inner face (tangent to it at
# the lid's end), turns across in front of the raised caseback, and curves into its floor, which is the
# underside of the lid's plate. Two turned ends and a straight middle, all one section.
_removed = 0.0
_cz = SCOOP_FLOOR_Z - SCOOP_BLEND + 0.5
_k = SCOOP_BLEND * math.sqrt(0.5)
_n = 0
for sx in (1, -1):
    for sy in (1, -1):
        _n += 1
        sk = sketch("LidScoopSketch%d" % _n, App.Placement(V(sx * END_X, sy * SCOOP_Y, 0), App.Rotation(V(1, 0, 0), -90)), lid)
        _c0 = SCOOP_R - SCOOP_BLEND
        sk.addGeometry([
            Part.LineSegment(V(0, SCOOP_FLOOR_Z + 0.5, 0), V(_c0, SCOOP_FLOOR_Z + 0.5, 0)),
            Part.ArcOfCircle(V(_c0, SCOOP_FLOOR_Z + 0.5, 0), V(_c0 + _k, _cz + _k, 0), V(SCOOP_R, _cz, 0)),
            Part.LineSegment(V(SCOOP_R, _cz, 0), V(SCOOP_R, _zb, 0)),
            Part.LineSegment(V(SCOOP_R, _zb, 0), V(0, _zb, 0)),
            Part.LineSegment(V(0, _zb, 0), V(0, SCOOP_FLOOR_Z + 0.5, 0)),
        ], False)
        _removed += cut_step("PartDesign::Groove", "LidScoop%d" % _n, sk, ReferenceAxis=(sk, ["V_Axis"]), Angle=360)
sk = sketch("LidScoopMidSketch", side_at(0), lid)
for sx in (1, -1):
    _xw, _xe = END_X - SCOOP_R, END_X + 0.5
    sk.addGeometry([
        Part.LineSegment(V(sx * _xw, _zb, 0), V(sx * _xw, _cz, 0)),
        Part.ArcOfCircle(V(sx * _xw, _cz, 0), V(sx * (_xw + SCOOP_BLEND - _k), _cz + _k, 0),
                         V(sx * (_xw + SCOOP_BLEND), SCOOP_FLOOR_Z + 0.5, 0)),
        Part.LineSegment(V(sx * (_xw + SCOOP_BLEND), SCOOP_FLOOR_Z + 0.5, 0), V(sx * _xe, SCOOP_FLOOR_Z + 0.5, 0)),
        Part.LineSegment(V(sx * _xe, SCOOP_FLOOR_Z + 0.5, 0), V(sx * _xe, _zb, 0)),
        Part.LineSegment(V(sx * _xe, _zb, 0), V(sx * _xw, _zb, 0)),
    ], False)
_removed += cut_step("PartDesign::Pocket", "LidScoopMid", sk, Type=0, Length=2 * SCOOP_Y, SideType="Symmetric")
log("LidScoop1..4, Mid      valid, 1 solid, volume %+.1f" % -_removed)
# the scoop must be open right across: nothing of the lid below the plate along its middle line
for sx in (1, -1):
    for yy in (0.0, SCOOP_Y - 0.3, -(SCOOP_Y - 0.3)):
        _probe = lid.Shape.common(Part.makeBox(0.6, 0.4, 3.0, V(sx * (END_X - 0.9) - 0.3, yy - 0.2, -(SCOOP_FLOOR_Z - 0.1) - 0.5)))
        assert _probe.Volume < 1e-6, ("scoop not open", sx, yy, _probe.Volume)


def scoop_s(x, y):
    x, y = abs(x), abs(y)
    return END_X - x if y <= SCOOP_Y else math.hypot(END_X - x, y - SCOOP_Y)


def scoop_prof(z):
    zc = SCOOP_FLOOR_Z - SCOOP_BLEND
    if z <= zc:
        return SCOOP_R
    return SCOOP_R - SCOOP_BLEND + math.sqrt(max(SCOOP_BLEND ** 2 - (z - zc) ** 2, 0.0))


# round the lower edges: the outer one runs from the flank's start (up the fan's curve) along the flank,
# the inner one runs round the scoop's mouth from one lug to the other. Same radius as the cheeks' edges.
def lug_outer_edge(e):
    if e.Length < 1:
        return False
    pts = e.discretize(8)
    xs = [abs(p.x) for p in pts]
    if max(xs) - min(xs) < 0.5 or min(xs) < ROOT_X - 0.02:
        return False
    if min(-p.z - 0.5 for p in pts) > LID_IN_Z - 0.05:    # the flank face's top edge, at the rim
        return False
    return all(abs(abs(p.y) - flank_y(abs(p.x))) < 0.02 for p in pts)


def lug_inner_edge(e):
    if e.Length < 0.3:
        return False
    for p in e.discretize(8):
        z = -p.z - 0.5
        if abs(z - under_z(p.x)) > 0.03 or abs(scoop_s(p.x, p.y) - scoop_prof(z)) > 0.03:
            return False
    return True


_cheeks = doc.getObject("SculptShoulderAndCheeks") or doc.getObject("SculptCheekEdges")
_cr = _cheeks.Radius.Value if _cheeks else 1.5


def round_edges(name, test, radii):
    base = lid.Tip
    names = edge_names(base.Shape, test)
    done = None
    for r in radii:
        if not names:
            break
        f = lid.newObject("PartDesign::Fillet", name)
        f.Base = (base, names)
        f.Radius = r
        f.Refine = True
        lid.Tip = f
        doc.recompute()
        sh = f.Shape
        if f.isValid() and not sh.isNull() and sh.isValid() and len(sh.Solids) == 1 and sh.Volume < base.Shape.Volume:
            done = r
            break
        lid.removeObject(f)
        doc.removeObject(f.Name)
        lid.Tip = base
        doc.recompute()
    log("%-22s %d edges, radius %s (cheek edges: %.2f)" % (name, len(names), ("%.2f" % done) if done else "did not build", _cr))
    return done


round_edges("LidLugRound", lug_outer_edge, (_cr, 1.2, 0.9, 0.6))
round_edges("LidGapRound", lug_inner_edge, (_cr, 1.2, 0.9, 0.6))

# the lug roots cover the four screws: open a well through each for the screw head, and the hole for its shaft
floor_z = -(LID_IN_Z - LID_RECESS_FLOOR) - 0.5           # the recess floor in lid coordinates
for name, r, length, want in (("LidLugWells", LID_RECESS_R, 8.0, 0), ("LidLugHoles", LID_HOLE_R, 1.0, 1)):
    s = sketch(name + "Sketch", App.Placement(V(0, 0, floor_z), App.Rotation()), lid)
    for sx in (1, -1):
        for sy in (1, -1):
            s.addGeometry(Part.Circle(V(sx * LID_SCREW_X, sy * LID_SCREW_Y, 0), V(0, 0, 1), r), False)
    f = lid.newObject("PartDesign::Pocket", name)
    f.Profile = s
    f.Length = length
    f.Refine = True
    lid.Tip = f
    doc.recompute()
    if screw_columns(lid.Shape)[want] > 0.01:            # went the wrong way
        f.Reversed = not f.Reversed
        doc.recompute()
head, shaft, seat = screw_columns(lid.Shape)
hollow_after = lid.Shape.common(hollow).Volume
log("lid lug roots: valid=%s solids=%d volume %+.1f; cheek gap %.2f mm; underside at the lid's end Z %.2f, on the cheek's back Z %.2f" % (
    lid.Shape.isValid(), len(lid.Shape.Solids), lid.Shape.Volume - lid_before, BAR_X - NOSE_R - END_X,
    under_z(END_X), under_z(BAR_X - NOSE_R)))
log("lid screws: in the heads' way %.3f, in the shafts' way %.3f (both must be 0), seats changed by %.3f, "
    "hollow under the caseback changed by %.3f mm3 (both must be 0); screw wells are %.1f mm deep" % (
        head, shaft, seat - seat_before, hollow_after - hollow_before,
        (LID_IN_Z - LID_RECESS_FLOOR) - under_z(LID_SCREW_X)))
assert head < 0.01 and shaft < 0.01 and abs(seat - seat_before) < 0.01 and abs(hollow_after - hollow_before) < 0.01
_lost = old_lid.cut(lid.Shape)
log("lid material that was there before and is gone: %.3f mm3 (round-off at the four screw wells only)" % _lost.Volume)
assert _lost.Volume < 0.1
# inside the plate (away from its chamfered edge) the lug roots may only have filled what the wells reopened
_room = gcyl(LUG_SPLIT_R - 0.15, 0, 0, LUG_SHELL_Z + 0.02, LID_IN_Z)
_inner = lid.Shape.cut(old_lid).common(_room)
log("lid material added in the caseback's hollow or the plate above it: %.3f mm3 (must be 0)" % _inner.Volume)
assert _inner.Volume < 0.1
_out = lid.Shape.cut(Part.makeBox(46, 44, 30, V(-23, -22, -20)))
log("lid outside X +-23: %.3f mm3 (must be 0)" % _out.Volume)

# ---------------------------------------------------------------- USB flap (separate part, meant for TPU)
flap = doc.addObject("PartDesign::Body", "UsbFlap")
flap.Label = "USB flap"
gside = App.Placement(V(0, 0, 0), App.Rotation(V(1, 0, 0), 90))      # global side view: x = X, y = Z
fx0 = END_X + FLAP_GAP
s = sketch("UsbFlapShellSketch", gside, flap)
shell_profile(s, +1, fx0)
f = flap.newObject("PartDesign::Pad", "UsbFlapShell")
f.Profile = s
f.SideType = "Symmetric"
f.Length = 2 * (GAP - FLAP_GAP)
doc.recompute()
# solid behind the shell, across the whole width: it covers the port and carries the grooves and the catch
inner_root = sweep_inner_z(fx0)
s = sketch("UsbFlapBlockSketch", gside, flap)
poly(s, [(fx0, inner_root + 0.6), (FLAP_BLOCK_X, sweep_inner_z(FLAP_BLOCK_X) + 0.3),
         (FLAP_BLOCK_X, FLAP_BASE_Z), (fx0, FLAP_BASE_Z)])
f = flap.newObject("PartDesign::Pad", "UsbFlapBlock")
f.Profile = s
f.SideType = "Symmetric"
f.Length = 2 * (GAP - FLAP_GAP)
doc.recompute()
cut = flap.newObject("PartDesign::SubtractiveLoft", "UsbFlapTop")
secs = top_sections(flap, "UsbFlapTop", True)
cut.Profile = secs[0]
cut.Sections = [secs[1]]
cut.Ruled = True
doc.recompute()
assert cut.Shape.isValid() and len(cut.Shape.Solids) == 1


def flap_cut(name, sk, **props):
    """A pocket on the flap; returns the volume it removed (and turns it round if it removed nothing)."""
    before = flap.Tip.Shape.Volume
    f = flap.newObject("PartDesign::Pocket", name)
    f.Profile = sk
    for k, v in props.items():
        setattr(f, k, v)
    f.Refine = True
    doc.recompute()
    if (f.Shape.isNull() or before - f.Shape.Volume < 1e-3) and "SideType" not in props:
        f.Reversed = not f.Reversed
        doc.recompute()
    assert not f.Shape.isNull() and f.Shape.isValid() and len(f.Shape.Solids) == 1, name
    return before - f.Shape.Volume


# grooves up the two sides, open at the bottom and closed at the top: they run on the cheeks' ribs, and
# the closed top lands on the rib's top, which sets how far down the flap goes
GROOVE_TOP_Z = RAIL_TOP_Z + 0.05
s = sketch("UsbFlapGroovesSketch", App.Placement(V(0, 0, FLAP_BASE_Z), App.Rotation()), flap)
for sy in (1, -1):
    _a, _b = sy * (GAP - RAIL_H - 0.2), sy * (GAP + 0.1)
    rect(s, RAIL_X0 - RAIL_FIT, min(_a, _b), RAIL_X0 + RAIL_W + RAIL_FIT, max(_a, _b))
_v = flap_cut("UsbFlapGrooves", s, Length=GROOVE_TOP_Z - FLAP_BASE_Z, Reversed=True)
log("UsbFlapGrooves         removed %.2f mm3 (two grooves, %.2f wide, %.2f deep, up to Z %.2f)" % (
    _v, RAIL_W + 2 * RAIL_FIT, RAIL_H + 0.2 - FLAP_GAP, GROOVE_TOP_Z))
assert 7.0 < _v < 11.0

# the spring: a strip across the back, cut free above, below and behind, still joined at both ends
SPRING_TOP_Z = PORT_TOP_Z + 0.12
SPRING_BOT_Z = SPRING_TOP_Z - SPRING_H
_xs, _xc, _slit = fx0 + SPRING_T, fx0 + SPRING_T + 0.8, 0.3
s = sketch("UsbFlapSpringSketch", gside, flap)
poly(s, [(fx0 - 0.5, SPRING_TOP_Z), (_xs, SPRING_TOP_Z), (_xs, SPRING_BOT_Z), (fx0 - 0.5, SPRING_BOT_Z),
         (fx0 - 0.5, SPRING_BOT_Z - _slit), (_xc, SPRING_BOT_Z - _slit), (_xc, SPRING_TOP_Z - 0.3),
         (_xs + 0.15, SPRING_TOP_Z + _slit), (fx0 - 0.5, SPRING_TOP_Z + _slit)])   # sloped roof: keeps the shell over it thick
_v = flap_cut("UsbFlapSpring", s, SideType="Symmetric", Length=2 * SPRING_HALF)
log("UsbFlapSpring          removed %.2f mm3; strip %.1f thick, %.1f tall, %.0f long, Z %.2f .. %.2f" % (
    _v, SPRING_T, SPRING_H, 2 * SPRING_HALF, SPRING_BOT_Z, SPRING_TOP_Z))
assert _v > 20
_strip = Part.makeBox(SPRING_T - 0.02, 2 * SPRING_HALF, SPRING_H - 0.02, V(fx0 + 0.01, -SPRING_HALF, SPRING_BOT_Z + 0.01))
assert flap.Tip.Shape.common(_strip).Volume > 0.99 * _strip.Volume          # the strip is still there
_room = Part.makeBox(0.7, 2 * SPRING_HALF - 0.1, SPRING_H - 0.1, V(_xs + 0.05, -SPRING_HALF + 0.05, SPRING_BOT_Z - 0.2))
assert flap.Tip.Shape.common(_room).Volume < 1e-3                           # with room behind it to bow into
flap_no_catch = flap.Tip.Shape.copy()

# the catch: a ridge on the middle of the strip. Its sloped underside rides down the end wall and bows the
# strip; at the bottom it springs in under the port's top edge. Its top is sloped too, so a firm push up
# from under the skirt lets it go.
_tipx = END_X - CATCH_REACH
s = sketch("UsbFlapCatchSketch", gside, flap)
poly(s, [(fx0 + 0.2, SPRING_TOP_Z - 0.02), (fx0, SPRING_TOP_Z - 0.02), (_tipx, PORT_TOP_Z - 0.3),
         (_tipx, PORT_TOP_Z - 0.45), (fx0, PORT_TOP_Z - 1.05), (fx0 + 0.2, PORT_TOP_Z - 1.05)])
f = flap.newObject("PartDesign::Pad", "UsbFlapCatch")
f.Profile = s
f.SideType = "Symmetric"
f.Length = 5.0
f.Refine = True
doc.recompute()
assert f.Shape.isValid() and len(f.Shape.Solids) == 1 and f.Shape.BoundBox.XMin < _tipx + 0.01
log("UsbFlapCatch           ridge 5 wide, reaching to X %.2f (%.2f under the port's top edge)" % (_tipx, CATCH_REACH))

doc.recompute()
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
log("objects with errors: %s" % bad)
new = body.Shape.copy()
new_lid = lid.Shape.copy()
new_flap = flap.Shape.copy()
log("result: tip=%s solids=%d shells=%d (1 = no enclosed voids) valid=%s volume=%.1f" % (
    body.Tip.Name, len(new.Solids), len(new.Shells), new.isValid(), new.Volume))
log("USB flap: valid=%s solids=%d volume %.1f" % (new_flap.isValid(), len(new_flap.Solids), new_flap.Volume))
doc.save()


# ---------------------------------------------------------------- checks
def overlap(a, b):
    try:
        c = a.common(b)
        return abs(c.Volume) if c.Solids else 0.0
    except Exception:
        return 0.0


display = doc.getObject("Display_merged").Shape
pcb = doc.getObject("PCB_merged").Shape
log("overlap with PCB      new %.4f mm^3   (current case %.4f)" % (overlap(new, pcb), overlap(old_shape, pcb)))
inside = Part.makeBox(44.0, 60, 20, V(-21.4, -30, -12))        # the display's flat cable is drawn sticking out at -X
log("overlap with display, inside the case   new %.4f mm^3   (current case %.4f)" % (
    overlap(new, display.common(inside)), overlap(old_shape, display.common(inside))))
log("overlap with display's drawn cable outside the case   new %.4f mm^3" % overlap(new, display.cut(inside)))
log("overlap with battery  new %.4f mm^3" % overlap(new, doc.getObject("Body001").Shape))
log("overlap body / lid %.4f,  body / flap %.4f,  flap / lid %.4f,  flap / PCB %.4f mm^3" % (
    overlap(new, new_lid), overlap(new, new_flap), overlap(new_flap, new_lid), overlap(new_flap, pcb)))
log("overlap lid / PCB %.4f,  lid / display %.4f,  lid / battery %.4f mm^3" % (
    overlap(new_lid, pcb), overlap(new_lid, display), overlap(new_lid, doc.getObject("Body001").Shape)))
probe = Part.makeCylinder(15.38, 2.0, V(0, 0, -0.5))
log("glass pocket: material inside R15.38 above the ceiling  new %.4f mm^3, current %.4f mm^3" % (
    overlap(new, probe), overlap(old_shape, probe)))


# does the flap go on, and stay on?
def moved(shape, dx, dz):
    c = shape.copy()
    c.translate(V(dx, 0, dz))
    return c


_path = max(overlap(new, moved(flap_no_catch, 0, 0.25 * k)) for k in range(0, 41))
log("flap sliding up and off (catch bowed out of the way): worst overlap with the case %.4f mm3 (must be 0)" % _path)
log("flap seated: nearest the case %.3f mm; pulled 0.4 mm straight off the end it overlaps the ribs by %.3f mm3 (must be > 0); "
    "pushed 0.3 mm up it overlaps the port's edge by %.4f mm3 (must be > 0)" % (
        new.distToShape(new_flap)[0], overlap(new, moved(new_flap, 0.4, 0)), overlap(new, moved(new_flap, 0, 0.3))))
_e = 3000.0                                              # PLA, MPa
_i = SPRING_H * SPRING_T ** 3 / 12
_l = 2 * SPRING_HALF
log("catch spring: bows %.2f mm going on; force at the catch about %.1f N, strain %.1f %% (PLA is safe to roughly 1.5 %%)" % (
    CATCH_REACH, 192 * _e * _i * CATCH_REACH / _l ** 3, 100 * 12 * CATCH_REACH * SPRING_T / _l ** 2))
assert _path < 1e-3 and overlap(new, moved(new_flap, 0.4, 0)) > 0.05 and overlap(new, moved(new_flap, 0, 0.3)) > 0.005
web = Part.makeBox(1.2, 10, 1.0, V(22.0, -5, -8.09))
log("strip under the USB port: new %.3f mm^3, current %.3f mm^3" % (overlap(new, web), overlap(old_shape, web)))

# deck thickness over the cavity: material straight above points just under the ceiling
thin = (99.0, None)
for ix in range(-43, 45):
    for iy in range(-38, 39):
        x, y = ix * 0.5, iy * 0.5
        r = math.hypot(x, y)
        in_cavity = (r < 19.0 or (abs(y) < 8.4 and -21.3 < x < 22.0)) and r > 15.6
        if not in_cavity:
            continue
        seg = Part.makeLine(V(x, y, -1.6), V(x, y, 3))
        t = sum(e.Length for e in new.common(seg).Edges)
        if t < thin[0]:
            thin = (t, (x, y))
log("thinnest deck over the cavity: %.2f mm at X=%.1f Y=%.1f" % (thin[0], thin[1][0], thin[1][1]))
gap = (99.0, None)
for v in display.Vertexes:
    p = v.Point
    if p.x < -21.4 or math.hypot(p.x, p.y) < 15.6 or p.z < -2.2:
        continue
    seg = Part.makeLine(V(p.x, p.y, p.z + 0.001), V(p.x, p.y, 3))
    c = new.common(seg)
    air = min([e.BoundBox.ZMin for e in c.Edges], default=3) - p.z
    if air < gap[0]:
        gap = (air, p)
log("least air above any display corner: %.2f mm at (%.2f, %.2f, %.2f)" % (gap[0], gap[1].x, gap[1].y, gap[1].z))

# ---------------------------------------------------------------- exports
mesh, deflection = None, None
for d in (0.01, 0.02, 0.015, 0.03, 0.008, 0.025):
    mm = MeshPart.meshFromShape(Shape=new, LinearDeflection=d, AngularDeflection=0.2, Relative=False)
    if not mm.isSolid() or mm.hasNonManifolds():
        continue
    if not mm.hasSelfIntersections():
        mesh, deflection = mm, d
        break
    if mesh is None:
        mesh, deflection = mm, d
bb = mesh.BoundBox
log("size: X %.2f..%.2f (%.1f end to end)  Y %.2f..%.2f  Z %.2f..%.2f" % (
    bb.XMin, bb.XMax, bb.XLength, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
log("print mesh: chord error %.3f, facets %d, solid=%s, non-manifold=%s, self-intersections=%s" % (
    deflection, mesh.CountFacets, mesh.isSolid(), mesh.hasNonManifolds(), mesh.hasSelfIntersections()))
new.exportStep(os.path.join(HERE, NAME + ".step"))
new_lid.exportStep(os.path.join(HERE, NAME + "-lid.step"))
new_flap.exportStep(os.path.join(HERE, NAME + "-usb-flap.step"))
mesh.translate(0, 0, -bb.ZMin)
mesh.write(os.path.join(HERE, NAME + ".3mf"))


def view_mesh(shape):
    vm = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.03, AngularDeflection=0.3, Relative=False)
    return {"p": [round(c, 3) for p in vm.Points for c in (p.x, p.z, -p.y)],
            "i": [i for fc in vm.Facets for i in fc.PointIndices]}


data = view_mesh(new)
data["lid"] = view_mesh(new_lid)
data["flap"] = view_mesh(new_flap)
json.dump(data, open(os.path.join(os.path.dirname(HERE), "lug-studies", "cad-body.json"), "w"), separators=(",", ":"))
log("done")
