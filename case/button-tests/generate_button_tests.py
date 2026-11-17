"""Generate print-in-place button test cases from case.FCStd.

Run with FreeCAD's command-line interpreter (it does not modify case.FCStd):

    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" -c "exec(open(r'case/button-tests/generate_button_tests.py').read())"

The case is taken from `Pocket005` (the last feature before the existing
button/strip features) so every variant starts from the same bare wall.

Local button frame used by every design:
    t  along the wall (horizontal)
    h  print height above the bed (the open rim sits on the bed, display face up,
       matching case-bambu.3mf)
    n  outward, 0 = tip of the tact-switch actuator; the wall spans n ~0.78..1.57

Designs (one per button position in case-mixed, or all four in case-<design>):
    A  flex tongue cut from the wall, hinged near the display face, free at the rim
    B  hanging leaf finger behind the wall + diamond cap through a diamond hole
    C  captive diamond plunger standing on the bed (no spring; switch returns it)
    D  current design repaired: rectangle cap, 0.45 vertical / 0.30 side gaps,
       original 0.3 mm strip, two break-away sprues under the cap
    E  door tongue (chosen design): hinge is a vertical line towards the middle of
       the long edge, so print layers run along the tongue instead of being pulled
       apart as in A. Sized for PETG. case.FCStd holds the same design as features.
"""

import math
import os
import sys

import FreeCAD as App
import Mesh
import MeshPart
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\James\Documents\git\epaper-smart-watch\case\button-tests"
CASE = os.path.join(os.path.dirname(HERE), "case.FCStd")
OUT = HERE
LOG = open(os.path.join(OUT, "report.txt"), "w")


def log(*a):
    LOG.write(" ".join(str(x) for x in a) + "\n")
    LOG.flush()


# ---------------------------------------------------------------- parameters
GAP_XY = 0.35      # gap between vertical faces (prints clean in XY)
GAP_45 = 0.30      # perpendicular gap on 45 deg faces (= 0.42 vertical)
GAP_Z = 0.45       # flat horizontal gap (>= 2 layers at 0.2 mm, absorbs bridge sag)
PRELOAD = 0.05     # rest gap to the switch actuator tip
MIN_THK = 0.38     # minimum thickness of fingers / flanges behind the wall
DOME_HALF = 0.9    # actuator dome half width (t); outside it the switch body is at n=-0.70
DIP_LIMIT = -0.35  # deepest a part may sit (PCB edge -0.43, switch body -0.70)
CAP_OUT = 2.30     # how far caps/pads stick out (n); wall outer face is ~1.57

# A - flex tongue
A_HALF_W = 2.0     # tongue half width (t)
A_SLOT_TOP = 6.0   # slot height from rim (tongue length / hinge position)
A_HINGE = (3.4, 5.6)  # h range where the tongue's inner face is thinned
A_HINGE_T = 0.48   # remaining wall thickness in the hinge zone
# E - door tongue (PETG: slightly wider gaps for ooze)
E_FREE = 2.0       # free edge distance from the button centre (t)
E_LENGTH = 6.0     # hinge distance from the button centre (t)
E_HEIGHT = 3.7     # tongue height from the rim (h)
E_SLOT = 0.40      # vertical slot at the free edge
E_TOP_GAP = 0.55   # horizontal slot above the tongue (bridged), round hinge end
# B - hanging finger
B_HALF_W = 1.8
B_ANCHOR = (5.6, 6.3)
DIAMOND_HOLE = 1.5  # half-diagonal of diamond hole (B, C)
# C - captive plunger
C_FLANGE_HALF_W = 2.1
C_FLANGE_TOP = 4.4
# D - repaired rectangle
D_HOLE = (2.0, 1.3)  # half width, half height
D_SIDE_GAP = 0.30
D_SPRUE = 0.30


# ---------------------------------------------------------------- frames
doc = App.openDocument(CASE)
base = doc.getObject("Pocket005").Shape.copy()
ZRIM = base.BoundBox.ZMax
BUTTON_Z = doc.getObject("ButtonParameters").ButtonHeight.Value
HC = ZRIM - BUTTON_Z  # print height of button centre
Z = App.Vector(0, 0, 1)
log("ZRIM %.3f  button centre print height %.3f" % (ZRIM, HC))


class Frame:
    def __init__(self, name, cx, cy, T, N):
        self.name, self.C = name, App.Vector(cx, cy, 0)
        self.T, self.N = T, N

    def p(self, t, h, n):
        return self.C + self.T * t + self.N * n + Z * (ZRIM - h)

    # wall profile sampled at mid height, filled in by sample_wall()
    def n_in(self, t):
        return _interp(self.prof, t, 0)

    def n_out(self, t):
        return _interp(self.prof, t, 1)


def _interp(prof, t, k):
    for (t0, a), (t1, b) in zip(prof, prof[1:]):
        if t0 <= t <= t1:
            f = (t - t0) / (t1 - t0)
            return a[k] + (b[k] - a[k]) * f
    raise ValueError("t %.2f outside sampled wall profile" % t)


def make_frames():
    frames = []
    for side in ("Left", "Right"):
        sk = doc.getObject(side + "HoleMasterSketch")
        rot = sk.Placement.Rotation
        N, T = rot.multVec(App.Vector(0, 0, 1)), rot.multVec(App.Vector(1, 0, 0))
        c = sk.Placement.Base
        frames.append(Frame(side + "+Y", c.x, c.y, T, N))
        frames.append(Frame(side + "-Y", c.x, -c.y,
                            App.Vector(T.x, -T.y, 0), App.Vector(N.x, -N.y, 0)))
    return frames


def sample_wall(f):
    prof = []
    for i in range(-40, 41):
        t = i * 0.1
        P = f.p(t, 4.0, 0)
        com = base.common(Part.makeLine(P - f.N * 4, P + f.N * 4))
        segs = sorted(sorted(((e.Vertexes[0].Point - P).dot(f.N), (e.Vertexes[-1].Point - P).dot(f.N)))
                      for e in com.Edges)
        seg = [s for s in segs if s[1] > 0.0]
        prof.append((t, tuple(seg[0])))
    f.prof = prof
    log("%s wall n_in/n_out: t=-2.5 %.2f/%.2f  t=0 %.2f/%.2f  t=+2.5 %.2f/%.2f" % (
        f.name, f.n_in(-2.5), f.n_out(-2.5), f.n_in(0), f.n_out(0), f.n_in(2.5), f.n_out(2.5)))


# ---------------------------------------------------------------- primitives
def _solid(pts, vec):
    face = Part.Face(Part.makePolygon(pts + [pts[0]]))
    return face.extrude(vec)


def prism_th(f, poly, n0, n1):
    """Polygon in (t, h), extruded along n."""
    return _solid([f.p(t, h, n0) for t, h in poly], f.N * (n1 - n0))


def prism_hn(f, poly, t0, t1):
    """Polygon in (h, n), extruded along t."""
    return _solid([f.p(t0, h, n) for h, n in poly], f.T * (t1 - t0))


def box(f, t0, t1, h0, h1, n0, n1):
    return prism_th(f, [(t0, h0), (t1, h0), (t1, h1), (t0, h1)], n0, n1)


def band(f, t0, t1, lo, hi, h0, h1, step=0.1):
    """Region t0..t1 between n=lo(t) and n=hi(t), extruded over h0..h1."""
    k = max(2, int(round((t1 - t0) / step)))
    ts = [t0 + (t1 - t0) * i / k for i in range(k + 1)]
    pts = [f.p(t, h0, lo(t)) for t in ts] + [f.p(t, h0, hi(t)) for t in reversed(ts)]
    return _solid(pts, -Z * (h1 - h0))


def diamond(f, half, n0, n1):
    return prism_th(f, [(0, HC - half), (half, HC), (0, HC + half), (-half, HC)], n0, n1)


def inner_face(hi):
    """Inner (switch-side) face for a plate whose outer face is hi(t): flat contact
    face over the actuator dome, dipping behind the switch plane elsewhere so the
    plate keeps MIN_THK where the wall curves in."""
    def lo(t):
        if abs(t) <= DOME_HALF:
            return PRELOAD
        return max(DIP_LIMIT, min(PRELOAD, hi(t) - MIN_THK))
    return lo


# ---------------------------------------------------------------- designs
# each returns (cuts, adds); adds may include separate free parts
def design_A(f):
    w, s = A_HALF_W, GAP_XY
    cuts = [box(f, w, w + s, -1, A_SLOT_TOP, -1, 3), box(f, -w - s, -w, -1, A_SLOT_TOP, -1, 3),
            # thin the hinge from the inside
            band(f, -w, w, lambda t: f.n_in(t) - 1.0, lambda t: f.n_out(t) - A_HINGE_T, *A_HINGE)]
    adds = [
        # actuator nub: rib from the bed so it never overhangs
        band(f, -1.0, 1.0, lambda t: PRELOAD, lambda t: f.n_in(t) + 0.1, 0, HC + 0.8),
        # finger pad outside, 45 deg underside
        prism_hn(f, [(HC - 1.6, 1.35), (HC - 0.85, CAP_OUT - 0.2), (HC + 1.2, CAP_OUT - 0.2), (HC + 1.2, 1.35)],
                 -1.6, 1.6),
    ]
    return cuts, adds


def design_B(f):
    w = B_HALF_W
    cuts = [diamond(f, DIAMOND_HOLE, 0.3, 3)]
    finger_out = lambda t: f.n_in(t) - GAP_XY
    adds = [
        band(f, -w, w, inner_face(finger_out), finger_out, 0, B_ANCHOR[1]),
        band(f, -w, w, inner_face(finger_out), lambda t: f.n_in(t) + 0.2, *B_ANCHOR),
        diamond(f, DIAMOND_HOLE - GAP_45 * math.sqrt(2), finger_out(0) - 0.05, CAP_OUT),
    ]
    return cuts, adds


def design_C(f):
    w = C_FLANGE_HALF_W
    cuts = [diamond(f, DIAMOND_HOLE, 0.3, 3)]
    flange_out = lambda t: f.n_in(t) - GAP_XY
    plunger = band(f, -w, w, inner_face(flange_out), flange_out, 0, C_FLANGE_TOP).fuse(
        diamond(f, DIAMOND_HOLE - GAP_45 * math.sqrt(2), flange_out(0) - 0.05, CAP_OUT))
    return cuts, [plunger]


def design_D(f):
    hw, hh = D_HOLE
    cw, ch = hw - D_SIDE_GAP, hh - GAP_Z
    cuts = [box(f, -hw, hw, HC - hh, HC + hh, 0.3, 3)]
    sp = D_SPRUE
    adds = [
        box(f, -6, 6, HC - 1.2412, HC + 1.2412, 0, 0.3),        # original strip
        box(f, -cw, cw, HC - ch, HC + ch, 0.2, CAP_OUT),          # cap
        # break-away sprues from hole floor to cap underside (cut with a knife)
        box(f, -1.0 - sp / 2, -1.0 + sp / 2, HC - hh - 0.05, HC - ch + 0.05, 1.57 - sp - 0.05, 1.52),
        box(f, 1.0 - sp / 2, 1.0 + sp / 2, HC - hh - 0.05, HC - ch + 0.05, 1.57 - sp - 0.05, 1.52),
    ]
    return cuts, adds


def design_E(f):
    s = -1 if f.name.startswith("Left") else 1  # hinge towards the middle of the edge
    tf, tf2 = -s * E_FREE, -s * (E_FREE + E_SLOT)
    r = E_TOP_GAP / 2
    tc, hm = s * (E_LENGTH - r), E_HEIGHT + r
    arc = [(tc + s * r * math.sin(math.pi * i / 24), hm - r * math.cos(math.pi * i / 24)) for i in range(25)]
    poly = [(tf2, -1), (tf, -1), (tf, E_HEIGHT)] + arc + [(tf2, E_HEIGHT + E_TOP_GAP)]
    cuts = [prism_th(f, poly, -3, 3)]
    adds = [
        box(f, -1.0, 1.0, 0, HC + 0.8, PRELOAD, 0.9),  # actuator nub, rib from the bed
        prism_hn(f, [(HC - 1.6, 1.35), (HC - 0.85, CAP_OUT - 0.2), (HC + 1.2, CAP_OUT - 0.2), (HC + 1.2, 1.35)],
                 -1.6, 1.6),
    ]
    return cuts, adds


DESIGNS = {"A": ("flex-tongue", design_A), "B": ("leaf-finger-diamond", design_B),
           "C": ("captive-plunger", design_C), "D": ("rect-repaired-sprues", design_D),
           "E": ("door-tongue", design_E)}


# ---------------------------------------------------------------- build
def build(assign):
    shape = base
    cuts, adds = [], []
    for f, key in assign:
        c, a = DESIGNS[key][1](f)
        cuts += c
        adds += a
    for c in cuts:
        shape = shape.cut(c)
    for a in adds:
        shape = shape.fuse(a)
    build.last_adds = adds
    return shape.removeSplitter()


def export(shape, stem):
    shape.exportStep(os.path.join(OUT, stem + ".step"))
    # print orientation: rim on the bed (rotate 180 deg about X, rim -> z=0)
    printed = shape.copy()
    printed.rotate(App.Vector(0, 0, 0), App.Vector(1, 0, 0), 180)
    printed.translate(App.Vector(0, 0, ZRIM))
    m = MeshPart.meshFromShape(Shape=printed, LinearDeflection=0.005, AngularDeflection=0.15, Relative=False)
    m.write(os.path.join(OUT, stem + ".3mf"))
    log("%s: solids=%d valid=%s volume=%.1f mesh_facets=%d" % (
        stem, len(shape.Solids), shape.isValid(), shape.Volume, m.CountFacets))


def check_interference(adds, label):
    """Overlap of the added button parts with the PCB (incl. switches), display, battery."""
    body_inv = doc.getObject("Body").Placement.inverse().toMatrix()
    for name in ("PCB_merged", "Display_merged", "Body001"):
        other = doc.getObject(name).Shape.copy()
        other.transformShape(body_inv)
        v = 0.0
        for a in adds:
            for s in other.Solids:
                if s.BoundBox.intersect(a.BoundBox):
                    c = a.common(s)
                    if c.Solids and abs(c.Volume) > 1e-4:
                        v += abs(c.Volume)
                        log("    overlap %.4f at %s" % (c.Volume, c.BoundBox))
        log("  %s interference with %s: %.4f mm^3" % (label, name, v))


frames = make_frames()
for f in frames:
    sample_wall(f)

baseline = base.Volume
log("bare case volume %.1f" % baseline)

# mixed: one design per button so a single print with the real PCB tests all
mixed = list(zip(frames, ["A", "B", "C", "D"]))
log("case-mixed assignment: " + ", ".join("%s=%s(%s)" % (f.name, k, DESIGNS[k][0]) for f, k in mixed))
shape = build(mixed)
export(shape, "case-mixed")
check_interference(build.last_adds, "case-mixed")

for key, (label, _) in DESIGNS.items():
    s = build([(f, key) for f in frames])
    export(s, "case-%s-%s" % (key, label))
    check_interference(build.last_adds, "case-" + key)

# keep the frames + profiles for the section plotter
import json
json.dump({"ZRIM": ZRIM, "HC": HC, "frames": [
    {"name": f.name, "C": [f.C.x, f.C.y], "T": [f.T.x, f.T.y], "N": [f.N.x, f.N.y]} for f in frames],
    "mixed": [[f.name, k] for f, k in mixed]}, open(os.path.join(OUT, "frames.json"), "w"), indent=1)
log("done")
LOG.close()
