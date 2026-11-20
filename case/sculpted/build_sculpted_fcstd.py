"""Rework a copy of case.FCStd into the sculpted case by editing its own steps.

    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" -c "exec(open(r'case/sculpted/build_sculpted_fcstd.py').read())"

Writes case/sculpted/case-sculpted-v13.FCStd (case.FCStd itself is not changed).

Existing steps that are changed
    Sketch001 (base outline)   wall moved out 1 mm (R20 -> R21, flanks offset), old lug tabs removed
    Sketch006 (deck boss)      was a R18.5 circle; now the whole outline, so the deck is at lip height everywhere
    ButtonParameters.PadOut    +1 mm, so the finger pads come through the thicker wall
    Lid Chamfer001             0.8 -> 0.95, carries the body's bottom bevel on down the lid
    sketch attachments         switched to fixed placements (they pointed at face numbers that move)

Existing steps that are deleted (with their sketches, where nothing else uses them)
    Pocket016, Pocket033       top and underside curves of the old lugs
    Pocket022                  old spring bar holes
    SubtractivePipe, 001       old chamfer along the round flanks
    Chamfer                    old cone on the bezel lip

New steps
    SculptBottomBevel   chamfer   45 deg bevel round the rim, inserted right after the rim cut (Pocket027)
    SculptButtonWindow  pocket    groove on the INSIDE of the wall at the hinge end of each door tongue (0.8 left), after the button steps
    SculptCeilingDrop   pad       lowers the cavity ceiling at the two ends, where the top cone comes down
    SculptLugA / B      pad       lugs from one side-view sketch, flush with the case flanks
    SculptLugTrim       pocket    cuts the lugs' outer faces to the flank line
    SculptTopCone       loft cut  the sloped top; also shapes the lug tops
    SculptShoulder...   fillet    rounding where the cone meets the wall and round the lugs
    SculptStrapOpening  fillet    rounds the edges of the strap opening between the lugs
    SculptBarHoles      pocket    spring bar holes

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
DST = os.path.join(HERE, "case-sculpted-v13.FCStd")
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
LAND_R = 16.8        # cone starts here across the wrist
CONE = 0.51          # drop per mm of radius across the wrist (27 deg)
KX = 0.86            # slope along the strap is CONE * KX (24 deg)
CONE_BOTTOM_Z = -6.0
RIM_Z = -8.08
BOTTOM_BEVEL = 0.8   # 1.0 does not build; leaves 1.0 of rim for the lid to sit on
LID_CHAMFER = 0.95
CEILING_DROP = 0.85  # cavity ceiling -0.5 -> -1.35 at the ends (display ears are at -1.64)
CEILING_DROP_X = 19.3
WINDOW_TOP_Z = -3.6   # hinge groove: from the rim up to here, just above the door slot (-3.83)
WINDOW_MARGIN = 0.7   # groove starts this far past the hinge line, clear of the round slot end
GROOVE_WIDTH = 4.0    # and runs this far along the tongue; the rest of the tongue stays full thickness
HINGE_THICKNESS = 1.0 # wall left at the groove, on the outside. Must exceed the bottom bevel (0.8), which
#                       takes that much off the outside at the rim; at 0.8 the wall vanished there.
LUG_Y0 = 10.5
BAR_X, BAR_Z, BAR_R = 27.0, -11.3, 0.6
NOSE_R = 2.2         # 1.6 mm of wall round the bar hole
LUG_ROOT_X = 22.6
LUG_D = (24.5, 0.5)        # the front curve starts at this X, this far above the top cone and parallel to it,
#                            so the cone runs into the curve almost tangentially (no crease across the lug)
LUG_F = (23.5, -7.88)      # underside starts here: clear of the lid edge, 0.2 above the rim
SHOULDER_RADII = (1.5, 1.4, 1.3, 1.2, 0.9)
LUG_RADII = (1.5, 1.2, 0.9)
CREST_FILLET = 0.4
OUTLINE_BLEND_R = 0.0   # optional blend between round and straight flank in the base outline (0 = off)
BROW_RADII = (3.0, 2.0, 1.5, 1.0)      # crease across each lug top
OPENING_RADII = (1.0, 0.8, 0.6)        # edges of the strap opening between the lugs

# flank line after growing: y = FLANK_Y0 + FLANK_M * (x - FLANK_X0), for x > 0, y > 0
(_x1, _y1), (_x2, _y2) = OLD_FLANK
_len = math.hypot(_x2 - _x1, _y2 - _y1)
_nx, _ny = (_y1 - _y2) / _len, (_x1 - _x2) / _len       # outward normal
FLANK_M = (_y2 - _y1) / (_x2 - _x1)
FLANK_X0, FLANK_Y0 = _x1 + _nx * GROW, _y1 + _ny * GROW
R_NEW = R_OLD + GROW


def flank_y(x):
    return FLANK_Y0 + FLANK_M * (x - FLANK_X0)


def cone_half(z):
    return LAND_R + (TOP_Z - z) / CONE


# ---------------------------------------------------------------- open the copy
shutil.copyfile(SRC, DST)
doc = App.openDocument(DST)
body = doc.getObject("Body")
lid = doc.getObject("Body003")
old_shape = body.Shape.copy()
old_lid = lid.Shape.copy()
log("start: volume %.1f" % old_shape.Volume)

# sketch attachments -> fixed placements (their supports are face numbers that would move)
for o in list(body.Group) + list(lid.Group):
    if o.TypeId == "Sketcher::SketchObject" and o.MapMode != "Deactivated":
        pl = App.Placement(o.Placement)
        o.MapMode = "Deactivated"
        o.Placement = pl
doc.recompute()
log("after freeing sketch attachments: volume %.1f (must equal start)" % body.Shape.Volume)


def outline_geometry():
    """Closed outline of the grown case, without lug tabs."""
    # The round flank (R_NEW) used to meet the straight flank at a 34 deg kink. A blend arc of
    # radius OUTLINE_BLEND_R, tangent to both, removes the kink so the shoulder rounding runs smoothly.
    # (Switched off by default: with the blend in, the big shoulder-and-lug fillet no longer builds.)
    if OUTLINE_BLEND_R <= 0:
        a = 1 + FLANK_M ** 2
        b = 2 * FLANK_M * (FLANK_Y0 - FLANK_M * FLANK_X0)
        c = (FLANK_Y0 - FLANK_M * FLANK_X0) ** 2 - R_NEW ** 2
        xa = (-b + math.sqrt(b * b - 4 * a * c)) / (2 * a)
        ya, ye0 = flank_y(xa), flank_y(END_X)
        a0 = math.atan2(ya, xa)
        big = Part.Circle(V(0, 0, 0), V(0, 0, 1), R_NEW)
        return [
            Part.ArcOfCircle(big, a0, math.pi - a0),
            Part.LineSegment(V(-xa, ya, 0), V(-END_X, ye0, 0)),
            Part.LineSegment(V(-END_X, ye0, 0), V(-END_X, -ye0, 0)),
            Part.LineSegment(V(-END_X, -ye0, 0), V(-xa, -ya, 0)),
            Part.ArcOfCircle(big, math.pi + a0, 2 * math.pi - a0),
            Part.LineSegment(V(xa, -ya, 0), V(END_X, -ye0, 0)),
            Part.LineSegment(V(END_X, -ye0, 0), V(END_X, ye0, 0)),
            Part.LineSegment(V(END_X, ye0, 0), V(xa, ya, 0)),
        ]
    rf = OUTLINE_BLEND_R
    norm = math.hypot(FLANK_M, 1)
    nx, ny = -FLANK_M / norm, 1 / norm                 # outward normal of the flank line
    ux, uy = ny, -nx                                   # along the line, towards +x
    h = nx * FLANK_X0 + ny * FLANK_Y0                  # distance of the line from the centre
    # It is an inside corner (the straight flank heads out of the circle), so the blend arc is
    # centred outside the body: R_NEW + rf from the centre, rf outside the flank line.
    t = math.sqrt((R_NEW + rf) ** 2 - (h + rf) ** 2)
    bx, by = t * ux + (h + rf) * nx, t * uy + (h + rf) * ny     # blend arc centre
    bl = math.hypot(bx, by)
    cxp, cyp = bx / bl * R_NEW, by / bl * R_NEW        # tangent point on the round flank
    lxp, lyp = bx - rf * nx, by - rf * ny              # tangent point on the straight flank
    ye = flank_y(END_X)
    ang = math.atan2(cyp, cxp)
    b0 = math.atan2(cyp - by, cxp - bx)
    b1 = math.atan2(lyp - by, lxp - bx)
    circle = Part.Circle(V(0, 0, 0), V(0, 0, 1), R_NEW)

    def P(x, y):
        return V(x, y, 0)

    def blend(sx, sy):
        a0, a1 = b0, b1                                # first quadrant: from the big arc round to the line
        if sx < 0:
            a0, a1 = math.pi - a1, math.pi - a0
        if sy < 0:
            a0, a1 = -a1, -a0
        return Part.ArcOfCircle(Part.Circle(V(sx * bx, sy * by, 0), V(0, 0, 1), rf), a0, a1)

    return [
        Part.ArcOfCircle(circle, ang, math.pi - ang),
        blend(-1, 1),
        Part.LineSegment(P(-lxp, lyp), P(-END_X, ye)),
        Part.LineSegment(P(-END_X, ye), P(-END_X, -ye)),
        Part.LineSegment(P(-END_X, -ye), P(-lxp, -lyp)),
        blend(-1, -1),
        Part.ArcOfCircle(circle, math.pi + ang, 2 * math.pi - ang),
        blend(1, -1),
        Part.LineSegment(P(lxp, -lyp), P(END_X, -ye)),
        Part.LineSegment(P(END_X, -ye), P(END_X, ye)),
        Part.LineSegment(P(END_X, ye), P(lxp, lyp)),
        blend(1, 1),
    ]


def replace_geometry(sk, geo):
    sk.deleteAllConstraints()
    sk.deleteAllGeometry()
    sk.addGeometry(geo, False)


# ---------------------------------------------------------------- edit the existing steps
# Steps the redesign replaces are deleted, together with any sketch nothing else uses.
# The first eight are the old button design, already switched off in case.FCStd (later steps first).
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
    users = [u.Name for u in o.InList if u is not body and u.TypeId != "App::DocumentObjectGroup"]
    if users:
        log("kept sketch %s (%s): still used by %s" % (o.Name, o.Label, users))
    else:
        log("removed sketch %s (%s)" % (o.Name, o.Label))
        body.removeObject(o)
        doc.removeObject(o.Name)
replace_geometry(doc.getObject("Sketch"), outline_geometry())       # Sketch001, base outline
replace_geometry(doc.getObject("Sketch005"), outline_geometry())    # Sketch006, deck up to the lip
vs = doc.getObject("ButtonParameters")
vs.PadOut = vs.PadOut.Value + GROW
doc.getObject("Chamfer001").Size = LID_CHAMFER
assert HINGE_THICKNESS >= BOTTOM_BEVEL + 0.2, "hinge wall must be thicker than the bottom bevel, or it vanishes at the rim"
doc.recompute()
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
log("after editing base steps: volume %.1f, errors %s" % (body.Shape.Volume, bad))


def sketch(name, placement):
    s = body.newObject("Sketcher::SketchObject", name)
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
    """Fillet or chamfer on named edges of `base`; tries each value until one builds."""
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
                # a fillet can pass isValid() and still break the next step, so try a cut through
                # each end of the case (where the bar holes will go) and see that it stays sound
                for sx in (1, -1):
                    probe = sh.cut(Part.makeBox(4, 60, 6, V(sx * 27 - 2, -30, 9)))
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
        r = math.hypot(p.x, p.y)
        on_arc = abs(r - R_NEW) < 0.01
        on_end = abs(abs(p.x) - END_X) < 0.01 and abs(p.y) <= flank_y(END_X) + 0.01
        on_flank = abs(abs(p.y) - flank_y(abs(p.x))) < 0.01
        if not (on_arc or on_end or on_flank):
            return False
    return True


dressup("PartDesign::Chamfer", "SculptBottomBevel", rim_cut, edge_names(rim_cut.Shape, rim_outer_edge),
        "Size", (BOTTOM_BEVEL, 0.8), insert_after=rim_cut)

# ---------------------------------------------------------------- button windows, before the door slots
first_slot = doc.getObject("InsertLeadInBore")   # groove goes after the button steps, before the last original step
window_sketch = doc.addObject("Sketcher::SketchObject", "SculptButtonWindowSketch")
body.insertObject(window_sketch, first_slot, False)
window_sketch.MapMode = "Deactivated"
window_sketch.Placement = plan_at(-WINDOW_TOP_Z)
window_sketch.Visibility = False
for slot_name in ("LeftDoorSlotSketch", "RightDoorSlotSketch"):
    sk = doc.getObject(slot_name)
    xs = []
    for g in sk.Geometry:
        for attr in ("StartPoint", "EndPoint"):
            if hasattr(g, attr):
                xs.append(getattr(g, attr).x)
        if hasattr(g, "Radius"):
            xs += [g.Center.x - g.Radius, g.Center.x + g.Radius]
    # groove only at the hinge end of the tongue (the round end of the slot), not over the whole tongue
    hinge = [g.Center.x for g in sk.Geometry if hasattr(g, "Radius")][0]
    away = -1.0 if hinge > 0 else 1.0
    t0, t1 = sorted((hinge - away * WINDOW_MARGIN, hinge + away * GROOVE_WIDTH))
    c = sk.Placement.Base
    tdir = sk.Placement.Rotation.multVec(V(1, 0, 0))
    angs = sorted(math.atan2(c.y + tdir.y * t, c.x + tdir.x * t) for t in (t0, t1))
    for mirror in (1, -1):
        a0, a1 = (angs[0], angs[1]) if mirror > 0 else (-angs[1], -angs[0])
        ri, ro = 19.0, R_NEW - HINGE_THICKNESS   # cut from the inside; the outer face stays whole
        window_sketch.addGeometry([
            Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), ri), a0, a1),
            Part.ArcOfCircle(Part.Circle(V(0, 0, 0), V(0, 0, 1), ro), a0, a1),
            Part.LineSegment(V(ri * math.cos(a0), ri * math.sin(a0), 0), V(ro * math.cos(a0), ro * math.sin(a0), 0)),
            Part.LineSegment(V(ri * math.cos(a1), ri * math.sin(a1), 0), V(ro * math.cos(a1), ro * math.sin(a1), 0)),
        ], False)
window = doc.addObject("PartDesign::Pocket", "SculptButtonWindow")
body.insertObject(window, first_slot, False)
window.Profile = window_sketch
window.Type = 1          # through all, towards the rim
window.Refine = True
doc.recompute()
# The cut must run from the sketch plane DOWN to the rim (over the tongue), never up through the top.
# Both directions remove material, so test where it was removed: nothing above the sketch plane may change.
above = Part.makeBox(80, 80, 20, V(-40, -40, -WINDOW_TOP_Z - 20.001))     # body z below the plane = above it


def cut_above():
    return window.BaseFeature.Shape.common(above).Volume - window.Shape.common(above).Volume


if cut_above() > 1e-3:
    window.Reversed = not window.Reversed
    doc.recompute()
log("%-22s valid=%s volume %+.1f, removed above the tongue zone %.3f (must be 0)" % (
    "SculptButtonWindow", window.Shape.isValid(),
    window.Shape.Volume - window.BaseFeature.Shape.Volume, cut_above()))
body.Tip = doc.getObject("InsertLeadInBore")
doc.recompute()
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
add.volume = body.Tip.Shape.Volume
log("body before the new end steps: volume %.1f, shells %d, errors %s" % (add.volume, len(body.Tip.Shape.Shells), bad))

# ---------------------------------------------------------------- cavity ceiling at the two ends
s = sketch("SculptCeilingDropSketch", plan_at(0.5))
rect(s, CEILING_DROP_X, -8.9, 22.4, 8.9)
rect(s, -21.8, -8.9, -CEILING_DROP_X, 8.9)
f = body.newObject("PartDesign::Pad", "SculptCeilingDrop")
f.Profile = s
f.Length = CEILING_DROP
add(f, +1)


# ---------------------------------------------------------------- lugs
def lug_front():
    """Front curve of the lug: leaves the top cone tangentially at D and runs tangent into the nose circle."""
    dx = LUG_D[0]
    r_d = math.hypot(dx * KX, LUG_Y0)
    dz = TOP_Z - (r_d - LAND_R) * CONE + LUG_D[1]           # LUG_D[1] above the cone at the inner face
    slope = -CONE * KX * KX * dx / r_d                       # slope of the cone along the strap there
    nx, nz = slope / math.hypot(slope, 1), -1 / math.hypot(slope, 1)

    def miss(r):
        return math.hypot(dx + r * nx - BAR_X, dz + r * nz - BAR_Z) - (r - NOSE_R)

    lo, hi = NOSE_R + 0.1, 60.0
    for _ in range(80):
        mid = (lo + hi) / 2
        if miss(mid) > 0:
            lo = mid
        else:
            hi = mid
    r = (lo + hi) / 2
    cx, cz = dx + r * nx, dz + r * nz
    d = math.hypot(BAR_X - cx, BAR_Z - cz)
    nose_x, nose_z = BAR_X + NOSE_R * (BAR_X - cx) / d, BAR_Z + NOSE_R * (BAR_Z - cz) / d
    return dx, dz, slope, cx, cz, r, nose_x, nose_z


LUG_FRONT = lug_front()


def lug_profile(s, sign):
    dx, dz, slope, cx, cz, big_r, nose_x, nose_z = LUG_FRONT
    a_d = math.atan2(dz - cz, dx - cx)
    a_n = math.atan2(nose_z - cz, nose_x - cx)
    a_nn = math.atan2(nose_z - BAR_Z, nose_x - BAR_X)
    az = dz + slope * (LUG_ROOT_X - dx)          # straight top, tangent to the front curve at D
    fx, fz = LUG_F
    d_ang = math.atan2(fz - BAR_Z, fx - BAR_X)   # underside: tangent from F to the nose circle
    t_ang = d_ang + math.acos(NOSE_R / math.hypot(fx - BAR_X, fz - BAR_Z))
    tx, tz = BAR_X + NOSE_R * math.cos(t_ang), BAR_Z + NOSE_R * math.sin(t_ang)

    def P(x, z):
        return V(sign * x, z, 0)

    def arc(cxx, czz, r, a0, a1):
        if sign < 0:
            a0, a1 = math.pi - a1, math.pi - a0
        return Part.ArcOfCircle(Part.Circle(V(sign * cxx, czz, 0), V(0, 0, 1), r), a0, a1)

    s.addGeometry([
        Part.LineSegment(P(LUG_ROOT_X, az), P(dx, dz)),
        arc(cx, cz, big_r, a_n, a_d),
        arc(BAR_X, BAR_Z, NOSE_R, t_ang - 2 * math.pi, a_nn),
        Part.LineSegment(P(tx, tz), P(fx, fz)),
        Part.LineSegment(P(fx, fz), P(LUG_ROOT_X, fz)),
        Part.LineSegment(P(LUG_ROOT_X, fz), P(LUG_ROOT_X, az)),
    ], False)


LUG_TIP_X = LUG_FRONT[3] + LUG_FRONT[5]
for tag, y in (("A", LUG_Y0), ("B", -LUG_Y0)):
    s = sketch("SculptLugSketch" + tag, side_at(y))
    lug_profile(s, +1)
    lug_profile(s, -1)
    f = body.newObject("PartDesign::Pad", "SculptLug" + tag)
    f.Profile = s
    f.Length = flank_y(LUG_ROOT_X) + 0.6 - LUG_Y0
    f.Reversed = (tag == "B")
    add(f, +1)

s = sketch("SculptLugTrimSketch", plan_at(0))
for sx in (1, -1):
    for sy in (1, -1):
        x0, x1 = LUG_ROOT_X - 0.6, LUG_TIP_X + 2
        poly(s, [(sx * x0, sy * flank_y(x0)), (sx * x1, sy * flank_y(x1)), (sx * x1, sy * 22), (sx * x0, sy * 22)])
f = body.newObject("PartDesign::Pocket", "SculptLugTrim")
f.Profile = s
f.Type = 1
f.SideType = "Symmetric"
add(f, -1)

# ---------------------------------------------------------------- the sloped top
cut = body.newObject("PartDesign::SubtractiveLoft", "SculptTopCone")
secs = []
for i, z in enumerate((TOP_Z + 0.4, CONE_BOTTOM_Z)):
    s = sketch("SculptTopConeSketch%d" % (i + 1), plan_at(-z))
    rect(s, -45, -35, 45, 35)
    s.addGeometry(Part.Ellipse(V(0, 0, 0), cone_half(z) / KX, cone_half(z)), False)
    secs.append(s)
cut.Profile = secs[0]
cut.Sections = [secs[1]]
cut.Ruled = True
add(cut, 0)


# ---------------------------------------------------------------- rounding
def on_cone(p, tol=0.02):
    return abs(-p.z - (TOP_Z - (math.hypot(p.x * KX, p.y) - LAND_R) * CONE)) < tol


def on_flank_plane(p, tol=0.02):
    return abs(abs(p.y) - flank_y(abs(p.x))) < tol


def shoulder_edge(e):
    if e.Length < 0.3 or e.BoundBox.ZMin < -TOP_Z + 0.05:
        return False
    pts = e.discretize(6)
    if not all(on_cone(p) for p in pts):
        return False
    return all(abs(math.hypot(p.x, p.y) - R_NEW) < 0.02 or on_flank_plane(p) for p in pts)


def lug_side_edge(e):
    if e.Length < 0.3:
        return False
    pts = e.discretize(6)
    return all(on_flank_plane(p) and abs(p.x) > END_X + 0.02 for p in pts) and not all(on_cone(p) for p in pts)


def arc_shoulder_edge(e):
    return shoulder_edge(e) and all(abs(math.hypot(p.x, p.y) - R_NEW) < 0.02 for p in e.discretize(6))


def on_front_curve(p, tol=0.02):
    return abs(math.hypot(abs(p.x) - LUG_FRONT[3], -p.z - LUG_FRONT[4]) - LUG_FRONT[5]) < tol and abs(p.x) > END_X


def on_nose(p, tol=0.02):
    return abs(math.hypot(abs(p.x) - BAR_X, -p.z - BAR_Z) - NOSE_R) < tol


def brow_edge(e):
    # where the cone-cut top of a lug meets its front curve
    if e.Length < 0.3:
        return False
    pts = e.discretize(6)
    return all(on_cone(p, 0.03) and on_front_curve(p, 0.03) for p in pts)


def strap_opening_edge(e):
    # top edge of the end wall between the lugs, and the inner edges of the lugs (top, front and nose)
    if e.Length < 0.3:
        return False
    pts = e.discretize(6)
    end_wall_top = all(abs(abs(p.x) - END_X) < 0.01 and abs(p.y) < LUG_Y0 + 0.01 and on_cone(p, 0.03) for p in pts)
    lug_inner = all(abs(abs(p.y) - LUG_Y0) < 0.01 and abs(p.x) > END_X + 0.01
                    and (on_cone(p, 0.03) or on_front_curve(p, 0.03) or on_nose(p, 0.03)) for p in pts)
    return end_wall_top or lug_inner


tip = body.Tip
both = dressup("PartDesign::Fillet", "SculptShoulderAndLugs", tip,
               edge_names(tip.Shape, lambda e: shoulder_edge(e) or lug_side_edge(e)), "Radius", SHOULDER_RADII)
if not both:
    tip = body.Tip
    if not dressup("PartDesign::Fillet", "SculptShoulder", tip, edge_names(tip.Shape, shoulder_edge),
                   "Radius", SHOULDER_RADII):
        dressup("PartDesign::Fillet", "SculptShoulder", tip, edge_names(tip.Shape, arc_shoulder_edge),
                "Radius", SHOULDER_RADII)
    tip = body.Tip
    dressup("PartDesign::Fillet", "SculptLugEdges", tip, edge_names(tip.Shape, lug_side_edge), "Radius", LUG_RADII)
# ---- the crease across each lug top, then the edges of the strap opening
tip = body.Tip
dressup("PartDesign::Fillet", "SculptLugBrow", tip, edge_names(tip.Shape, brow_edge), "Radius", BROW_RADII)
tip = body.Tip
if not dressup("PartDesign::Fillet", "SculptStrapOpening", tip, edge_names(tip.Shape, strap_opening_edge),
               "Radius", OPENING_RADII):
    # fall back to the top edges only
    dressup("PartDesign::Fillet", "SculptStrapOpening", tip,
            edge_names(tip.Shape, lambda e: strap_opening_edge(e) and all(on_cone(p, 0.03) for p in e.discretize(6))),
            "Radius", OPENING_RADII)

tip = body.Tip
dressup("PartDesign::Fillet", "SculptCrest", tip,
        edge_names(tip.Shape, lambda e: abs(e.BoundBox.ZMin + TOP_Z) < 1e-4 and abs(e.BoundBox.ZMax + TOP_Z) < 1e-4
                   and min(math.hypot(v.X, v.Y) for v in e.Vertexes) > LAND_R - 0.1), "Radius", (CREST_FILLET, 0.3))
add.volume = body.Tip.Shape.Volume

# ---------------------------------------------------------------- bar holes
s = sketch("SculptBarHolesSketch", side_at(0))
for sx in (1, -1):
    s.addGeometry(Part.Circle(V(sx * BAR_X, BAR_Z, 0), V(0, 0, 1), BAR_R), False)
f = body.newObject("PartDesign::Pocket", "SculptBarHoles")
f.Profile = s
f.SideType = "Two sides"
f.Length = LUG_Y0 + 0.5
f.Length2 = LUG_Y0 + 0.5
add(f, -1)

doc.recompute()
bad = [(o.Name, o.State) for o in doc.Objects if "Invalid" in o.State or "Error" in o.State]
log("objects with errors: %s" % bad)
new = body.Shape.copy()
new_lid = lid.Shape.copy()
log("result: tip=%s solids=%d shells=%d (1 = no enclosed voids) valid=%s volume=%.1f" % (
    body.Tip.Name, len(new.Solids), len(new.Shells), new.isValid(), new.Volume))
doc.save()


# ---------------------------------------------------------------- checks and exports
def overlap(a, b):
    try:
        c = a.common(b)
        return abs(c.Volume) if c.Solids else 0.0
    except Exception:
        return 0.0


for name, label in (("PCB_merged", "PCB"), ("Display_merged", "display"), ("Body001", "battery")):
    o = doc.getObject(name).Shape
    log("overlap with %-8s new %.4f mm^3   (current case %.4f mm^3)" % (label, overlap(new, o), overlap(old_shape, o)))
log("overlap body / lid       new %.4f mm^3   (current case %.4f mm^3)" % (overlap(new, new_lid), overlap(old_shape, old_lid)))
probe = Part.makeCylinder(15.38, 2.0, V(0, 0, -0.5))
log("glass pocket: material inside R15.38 above the ceiling  new %.4f mm^3, current %.4f mm^3" % (
    overlap(new, probe), overlap(old_shape, probe)))
# door tongues: material in the old wall (R19.2..R20) over the button zone must be as before
ring = Part.makeCylinder(R_OLD, 4.4, V(0, 0, RIM_Z)).cut(Part.makeCylinder(19.2, 4.4, V(0, 0, RIM_Z)))
band = ring.common(Part.makeBox(30, 60, 10, V(-15, -30, -10)))
log("door tongues: old-wall material in the button zone  new %.3f mm^3, current %.3f mm^3" % (
    overlap(new, band), overlap(old_shape, band)))

# print mesh: it must be a closed, manifold solid; among those prefer one with no overlapping facets
mesh, deflection = None, None
for d in (0.01, 0.02, 0.015, 0.03, 0.008, 0.025):
    m = MeshPart.meshFromShape(Shape=new, LinearDeflection=d, AngularDeflection=0.2, Relative=False)
    if not m.isSolid() or m.hasNonManifolds():
        continue
    if not m.hasSelfIntersections():
        mesh, deflection = m, d
        break
    if mesh is None:
        mesh, deflection = m, d
log("print mesh chord error %.3f mm" % deflection)
bb = mesh.BoundBox
log("size: X %.2f..%.2f (%.1f lug to lug)  Y %.2f..%.2f  Z %.2f..%.2f" % (
    bb.XMin, bb.XMax, bb.XLength, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
log("mesh: facets %d, solid=%s, non-manifold=%s, self-intersections=%s" % (
    mesh.CountFacets, mesh.isSolid(), mesh.hasNonManifolds(), mesh.hasSelfIntersections()))
new.exportStep(os.path.join(HERE, "case-sculpted-v13.step"))
new_lid.exportStep(os.path.join(HERE, "lid-sculpted-v13.step"))
mesh.translate(0, 0, -bb.ZMin)
mesh.write(os.path.join(HERE, "case-sculpted-v13.3mf"))


def view_mesh(shape):
    m = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.03, AngularDeflection=0.3, Relative=False)
    return {"p": [round(c, 3) for p in m.Points for c in (p.x, p.z, -p.y)],
            "i": [i for fc in m.Facets for i in fc.PointIndices]}


data = view_mesh(new)
data["lid"] = view_mesh(new_lid)
json.dump(data, open(os.path.join(os.path.dirname(HERE), "lug-studies", "cad-body.json"), "w"), separators=(",", ":"))
log("done")
