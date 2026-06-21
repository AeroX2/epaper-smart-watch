# Auto-layout / auto-route tooling

A from-scratch, headless pipeline that **auto-places** and **auto-routes** this board's
`pcb/epaper-smart-watch.kicad_pcb`, driven entirely by the KiCad 10 `pcbnew` Python API plus
[Freerouting](https://github.com/freerouting/freerouting). Built iteratively with Claude Code.

> **Run it on a COPY.** Every script that writes (`place*.py`, `dsn_export.py`, `ses_import.py`)
> loads and overwrites the board file pointed at by the `P=...` constant at the top of the script,
> which is a working copy under a temp dir — **never the live `pcb/` file**. Copy the board to your
> working path first; don't point these at the source of truth.

## Pipeline

```
              pcb/epaper-smart-watch.kicad_pcb   (source of truth — copy it first)
                              │  cp to working copy
                              ▼
   place4.py ───────────────────────────────► placed board   (auto-layout)
     │  (cluster placement: each IC + its passives as a logical group)
     ▼
   check_courtyards.py / measure_edge.py ────► verify: 0 overlaps, edge margin kept
     │
     ▼
   dsn_export.py ───────────────────────────► board.dsn      (Specctra)
     │
     ▼
   java -jar freerouting.jar -de board.dsn -do board.ses -mp 8
     │
     ▼
   ses_import.py ───────────────────────────► routed board   (tracks + vias added)
```

## Core scripts

| Script | Role |
|---|---|
| **`place4.py`** | **Main placer.** Hierarchical *cluster* placement — see below. |
| `place3.py` | Earlier placer: force-directed clustering + repulsion spread. Kept for reference. |
| `check_courtyards.py` | Extracts every footprint's **courtyard** (F.CrtYd, pad-bbox fallback) and reports overlapping pairs via convex-hull SAT. Side-aware. `python check_courtyards.py [board]` |
| `measure_edge.py` | Reports any part whose courtyard is `<0.5 mm` from / outside the board outline. |
| `measure_decoup.py` | Distance from each pure decoupling cap (both pins on power/GND) to its nearest IC. |
| `measure_prox.py` | Checks schematic-adjacent parts stay close on the PCB. |
| `probe_sides.py` | Lists which footprints are top / bottom / through-hole. |
| `dsn_export.py` | From-scratch **Specctra DSN** exporter (pcbnew's `ExportSpecctraDSN` hangs headless). |
| `ses_import.py` | From-scratch Freerouting **SES** importer (adds `PCB_TRACK` / `PCB_VIA`). |
| `pdftxt.py` | Datasheet PDF → text (used while reviewing the schematic). |

`scratch/` holds the one-off analysis/debug scripts written along the way (earlier placers,
pin-mapping, net tracing, render cropping, etc.) — kept for completeness, not maintained.

## How `place4.py` works

The goal: lay the board out as **logical clusters** — each IC/module with the passives that
support it — rather than a single global blob.

1. **Anchors** = active ICs/modules (`U#`, `IC#`, `AC#`) + key connectors (`J6`, `P1`, `SWD1`,
   `J5`, `M1`, `BZ1`).
2. **Assignment** — every passive joins the anchor it *electrically* belongs to: the IC it shares
   the most signal nets with (ICs preferred over hub connectors like the 24-pin display FPC), and
   for pure power/GND decoupling caps (no signal net) the nearest IC **in the schematic** (the
   `.kicad_sch` symbol coordinates capture the designer's grouping intent).
3. **Placement** — biggest parts first so the (very full) board doesn't fragment and everything
   lands; then a **pull-in** pass slides each passive to the nearest free spot by its own anchor,
   and a **swap** pass trades equal-size passives sitting in the wrong cluster (swaps need no free
   space, so they tighten a packed board).

Constraints enforced throughout:
- **Courtyard** extents (real keep-outs), not pad boxes, + `EXT_MARGIN` safety pad.
- **Top and bottom handled separately** — a top SMD and a bottom part may share the same X/Y;
  through-hole parts block both faces.
- **`EDGE_MARGIN`** — the whole courtyard (not just the centre) stays inside the outline.
- **RF keepout** — a clear radius around the STM32WB module's antenna (`U3`).
- Locked parts (`J6`, `P1`, `U2`, `U3`, `SW2`–`SW5`) are fixed anchors and never moved.

Result on this board: 0 courtyard overlaps, 0 edge violations, every part placed. Cohesion is
~8–9 mm average, which is near the geometric floor here — two clusters have 17 members each
(display-bias network on `J6`, MCU support on `U3`), and you can't pack 17 parts within a few mm
of one point. (Render top/bottom with `kicad-cli pcb export pdf` — PDFs aren't tracked in git.)

## Notes / caveats

- **Paths are hard-coded** at the top of each script (`P=` board, `SCH=` schematic). Adjust them.
- The board's **Edge.Cuts is not a closed contour**, so `GetBoardPolygonOutlines` returns nothing;
  the scripts rebuild the outline from Edge.Cuts segment endpoints (angle-sorted). Worth fixing in
  the actual board (it also breaks DRC / 3D / fab).
- **Freerouting** (`freerouting.jar`, v1.9.0) is not committed — download it from the
  [releases page](https://github.com/freerouting/freerouting/releases). `-mp 8` works; very high
  `-mp` values produced empty output in testing. Needs a Java runtime.
- Tested with **KiCad 10.0.3** on Windows.
