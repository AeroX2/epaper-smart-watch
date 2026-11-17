"""Section views of each button design (run with freecadcmd after the generator).

    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" -c "exec(open(r'case/button-tests/plot_sections.py').read())"

Writes sections-<design>.png: rows = Left+Y / Right+Y button, columns =
  1. horizontal slice through the button centre (t vs n, looking down in print)
  2. vertical slice through the button axis (n vs print height)
  3. vertical slice in the middle of the wall (t vs print height, as seen from outside)
Grey = tact switch / PCB from case.FCStd.
"""
import json
import os

import FreeCAD as App
import Part
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = r"C:\Users\James\Documents\git\epaper-smart-watch\case\button-tests"
meta = json.load(open(os.path.join(HERE, "frames.json")))
ZRIM, HC = meta["ZRIM"], meta["HC"]
doc = App.openDocument(os.path.join(os.path.dirname(HERE), "case.FCStd"))
pcb = doc.getObject("PCB_merged").Shape.copy()
pcb.transformShape(doc.getObject("Body").Placement.inverse().toMatrix())

Z = App.Vector(0, 0, 1)


def frame(name):
    for f in meta["frames"]:
        if f["name"] == name:
            return (App.Vector(f["C"][0], f["C"][1], 0), App.Vector(f["T"][0], f["T"][1], 0),
                    App.Vector(f["N"][0], f["N"][1], 0))


def local(C, T, N, p):
    return (p - C).dot(T), ZRIM - p.z, (p - C).dot(N)


def draw(ax, shape, C, T, N, normal, d, axes, color, lw=0.9, near=None):
    for w in shape.slice(normal, d):
        for e in w.Edges:
            pts = [local(C, T, N, p) for p in e.discretize(40)]
            xs = [p[axes[0]] for p in pts]
            ys = [p[axes[1]] for p in pts]
            ax.plot(xs, ys, color=color, lw=lw)


def panel_set(axs, shape, name):
    C, T, N = frame(name)
    sw = Part.makeCompound([s for s in pcb.Solids if s.BoundBox.isInside(C + N * -1 + Z * (ZRIM - HC)) or
                            (s.BoundBox.Center - (C + Z * (ZRIM - HC))).Length < 6])
    views = [
        ("slice h=%.2f (top view)" % HC, Z, ZRIM - HC, (0, 2), "t", "n", (-7.5, 7.5), (-2, 3)),
        ("slice t=0 (side)", T, C.dot(T), (2, 1), "n", "print height h", (-1.5, 3), (-0.3, 8)),
        ("slice h=3.98 (top slot)", Z, ZRIM - 3.98, (0, 2), "t", "n", (-7.5, 7.5), (-2, 3)),
    ]
    for ax, (title, nrm, d, axes, xl, yl, xr, yr) in zip(axs, views):
        draw(ax, sw, C, T, N, nrm, d, axes, "0.6", 0.7)
        draw(ax, shape, C, T, N, nrm, d, axes, "C0")
        ax.set_title("%s  %s" % (name, title), fontsize=8)
        ax.set_xlim(*xr); ax.set_ylim(*yr); ax.set_aspect("equal")
        ax.set_xlabel(xl, fontsize=7); ax.set_ylabel(yl, fontsize=7)
        ax.tick_params(labelsize=6); ax.grid(True, lw=0.3)


for fn in sorted(os.listdir(HERE)):
    if not (fn.startswith("case-") and fn.endswith(".step")):
        continue
    shape = Part.read(os.path.join(HERE, fn))
    names = ["Left+Y", "Right+Y"] if fn != "case-mixed.step" else [m[0] for m in meta["mixed"]]
    fig, axs = plt.subplots(len(names), 3, figsize=(13, 3.6 * len(names)))
    for row, name in zip(axs, names):
        panel_set(row, shape, name)
    if fn == "case-mixed.step":
        for row, (name, key) in zip(axs, meta["mixed"]):
            row[0].set_title("%s = design %s" % (name, key), fontsize=9, loc="left")
    fig.suptitle(fn, fontsize=10)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "sections-" + fn[5:-5] + ".png"), dpi=110)
    plt.close(fig)
