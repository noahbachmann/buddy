# parts/ – printable parts

Parametric OpenSCAD, one folder per module (`dispenser/`, `pc-case/` (#87), `hex-driver/`). A module is a folder with a
`src/cad.toml`. The CAD environment is built in #54: toolchain, `cad build` and `cad render` exist; `cad check/sweep/slice`
come next.

## Module layout

Dev opens a module to print: its root holds only the plates.

```
<module>/
├── 0-fit-test.stl …   ← print plates (`cad plate`): nothing else .stl here (a full `cad plate` deletes strays)
├── README.md
├── stl/               ← single parts (`cad build`)
├── skeleton/          ← pc-case only: the fit-test skeleton, a module of its own with the same layout
└── src/               ← cad.toml, .scad, scripts, reference models (`dispenser.stl`, `ref/`); renders/ (gitignored)
```

Paths in `cad.toml`, includes and scripts are relative to `src/`.

## Toolchain

`tools/setup.sh` (idempotent, pinned) installs everything; run it after cloning and when a pin changes.

| What | Where | Pin |
|---|---|---|
| OpenSCAD nightly, extracted AppImage | `~/.local/opt/openscad-<ver>`, linked as `~/.local/bin/openscad` | `OPENSCAD_VERSION` in `setup.sh` |
| BOSL2 | `lib/BOSL2` (submodule) | tag in the submodule commit |
| NopSCADlib | `lib/NopSCADlib` (submodule) | tag in the submodule commit |
| `cad` CLI + Python deps (trimesh, manifold3d, pyvista, shapely, matplotlib) | `tools/` (uv project), run via `parts/cad` | `tools/uv.lock` |

- Always `--backend=manifold`: ~135× faster than CGAL on a servo arm part (17.5 s → 0.13 s), same geometry.
- `OPENSCADPATH=parts/lib` so parts write `include <BOSL2/std.scad>`, `include <NopSCADlib/vitamins/…>`.
- Rendering is headless: VTK on EGL (the RTX 2060 here; CI without GPU: `VTK_DEFAULT_OPENGL_WINDOW=vtkOSOpenGLRenderWindow`).
  OpenSCAD's PNG export also runs without a display.
- Apt's OpenSCAD 2021.01 may still sit in `/usr/bin`; it has no Manifold backend, so builds fail loudly with it.

## `parts/cad`

```bash
parts/cad build dispenser [part…]                  # STLs into dispenser/stl (binary), stats per part; any WARNING fails
parts/cad plate dispenser [plate…]                 # print jobs from src/cad.toml [plates] → dispenser/*.stl
parts/cad render dispenser [part…]                 # PNG sheets into dispenser/src/renders/ (gitignored)
parts/cad render dispenser lift --section y=0 --section z=10       # + 2D cuts
parts/cad render dispenser -D push=0.5             # -D goes to every file rendered (poses for the assembly)
parts/cad render some.scad | some.stl [-o dir]     # loose files; a module's assembly file renders as assembly
cd parts/tools && uv run pytest -q                 # tool tests (fixtures in tests/fixtures)
```

- **Part sheet** (3 × 3): 2 iso, top, bottom, 4 sides (orthographic, mm scales), and an **x-ray from below** that shows
  only the problem faces through a faint body: the only place internal overhangs are visible. Colours: red =
  overhang > 45°, orange = flat downward face (bridge/roof), green = on the bed. Header: size, volume, max PETG mass
  (solid), bed/overhang/roof areas.
- **Section**: filled 2D cut, 1 mm / 10 mm grid, world axis names kept. Use it for walls, bearing seats, nut slots,
  cable channels: anything inside.
- **Plates** (`tools/src/cad/plate.py`): `[plates.<name>]` in `src/cad.toml` lists parts (repeat = copies; a list =
  one group kept in its common frame, e.g. parts that print in one frame) and a `note` (filament, settings). Parts
  turn only about Z, pack into rows on 230 × 230 (`bed = [w, d]` per plate for parts wider than that, ≤ 256) with 8 mm gaps, centred. One STL per plate = one object in Bambu
  Studio: settings apply to the whole plate, auto brim judges the whole plate. Dev prints from the module root.
  Shelves can't nest one part into another's corner: `place = [[x, y, turn°], …]` (one per item, bbox min corner
  after the turn) fixes the layout instead, `gap` sets the minimum distance; `cad plate` checks the real footprints
  against it and the bed (the PC case's covers used it, an L round a box, until they grew past one bed, 2026-11-28).
- **Assembly sheet**: OpenSCAD's own preview from 4 directions (gimbal camera), keeps `color()` and `%` ghosts.
- Print classes (`tools/src/cad/mesh.py`): +Z = build direction, lowest Z = bed, 45° counts as fine (teardrops,
  chamfers). Numbers are surface areas, not yet judged: `cad check` (next) adds the rules.

## Working loop (Claude)

After every modelling change: `cad build` → `cad render` → **Read the sheets** and say what each view confirmed;
section through every fit (bearing seat, slot, channel). Numbers can't see a missing hole; the sheet can.

## Rules

- STL only. 3MF files (e.g. `tray/tray.3mf`) are Dev's print projects: never read, write or regenerate them.
- STLs are committed already in print orientation (+Z up = build direction), binary. So are the plates: rebuild
  them (`cad plate <module>`) whenever a part on them changes.
- Dev drags a plate in and prints: no arranging. A part that needs support gets none in the file: Bambu Studio
  generates it. Turn supports on in that plate's `note` and README row.
- Every dimension in the module's `params.scad`; design rules and fits from `docs/3d-printing.md`.
- `tray/` has no source (Dev's own): change only on request, by editing the STL directly (lid rim gap: `docs/3d-printing.md` → Fits).
- `dispenser/src/dispenser.stl` is Dev's Onshape box: never modify it. Roller and pusher are built in its coordinates;
  `make_copy.py` writes `ref/` (box, lift, 4023 cut out of it) and the combined copy `dispenser-roller.stl`. The case
  (`case.scad`: `ref/box.stl` + `case_add()` − `box_cuts()`, prints on its back) (`case-spares.scad`: the same minus `spare_cuts()`, less filament; `case.stl` stays) and the lift (`lift.scad`) are ours
  now; cards/roller/lift positions differ from `dispenser.stl` (README top). The case prints with −Y up: anything
  facing +Y (the back wall) overhangs, horizontal holes are teardrops towards −Y (`hole_z`, `hole_x(front = true)`). `check_pusher.py` checks the pusher's stroke and the lift's travel for collisions.

## Gotchas

- trimesh sections need `networkx` (declared); a missing optional trimesh dep fails only when the feature is used.
- PIL's default font lacks × ² ³ ≤: sheets use matplotlib's DejaVu Sans.
- OpenSCAD's eye/centre `--camera` picks its own up vector (top view came out rotated): use the gimbal form.
- The same height reached by two different sums (`a + b + c` vs `(a + b) + c`) differs by 1e-14; where two such faces
  meet, the STL gets zero-area slivers and isn't watertight. Derive touching faces from one base + exact offsets.
  `box()` is translate + size, so its far face lands at `lo + (hi − lo)`, not `hi`. A part that must end flush with a
  cut face: union it before the cut and let the one cut make both faces (`box_trimmed()` + `notch_skin()`).
- Even with exact values, a union of two solids with coplanar faces (overlapping or touching), or a cut whose face
  lies in another face's plane, can leave duplicate vertices ~1e-9 apart: watertight in double, pinched (edges used 4×)
  in the float32 STL. Build such faces from one solid (`frame()`: plate + bed as one extruded L profile) or offset the
  cut by 0.01.
- The x-ray colours faces, not islands: a region that starts in mid-air over nothing (Bambu: "floating parts") can
  show as a small orange roof. Before a plate goes out, slice it in 0.2 mm layers and flag polygons that touch nothing
  in the layer below (2026-11-24: the roller frame's rib tongues started 2 mm up).
- trimesh's `polygons_full` can come back empty for valid rings (a cut exactly through a peg's axis in its socket):
  `render.section` then falls back to the closed rings, so check a blank section against the mesh.
- A param change rebuilds STLs in other parts than the one being worked on (fits are shared): commit every changed
  `stl/`, plate and `dispenser-roller.stl` with it. `git status` clean under `parts/` after a commit. Dev prints
  from GitHub: a stale plate there printed the borders with the pre-test fits (2026-11-11).
- A cut extruded `big` along its axis runs on through every wall behind it: bound cuts to their own feature (the PC
  case's nut pockets notched the far wall, 2026-11-24). Catch slivers by slicing and eroding each layer by 0.4 mm:
  a polygon that vanishes is thinner than two lines.
- Manifold booleans need watertight input: an open reference mesh (e.g. `dispenser/src/ref/lift.stl`) silently intersects
  as 0. Check `is_watertight` before trusting an overlap volume.
- BOSL2's thread `$slop` is radial (4·$slop on the diameter). On a TR thread that's ¼ at the flanks: size thread gaps per
  flank and convert (`tr8_thread()` in `dispenser/src/lib.scad`). Every bore from the fit values, never a picked number.
