# parts/ – printable parts

Parametric OpenSCAD, one folder per module (`arm/`, `dispenser/`, later `neck/`, `body/`, `head/`). A module is a folder with a
`cad.toml`. The CAD environment is built in #54: toolchain, `cad build` and `cad render` exist; `cad check/sweep/slice`
come next.

## Toolchain

`tools/setup.sh` (idempotent, pinned) installs everything; run it after cloning and when a pin changes.

| What | Where | Pin |
|---|---|---|
| OpenSCAD nightly, extracted AppImage | `~/.local/opt/openscad-<ver>`, linked as `~/.local/bin/openscad` | `OPENSCAD_VERSION` in `setup.sh` |
| BOSL2 | `lib/BOSL2` (submodule) | tag in the submodule commit |
| NopSCADlib | `lib/NopSCADlib` (submodule) | tag in the submodule commit |
| `cad` CLI + Python deps (trimesh, manifold3d, pyvista, shapely, matplotlib) | `tools/` (uv project), run via `parts/cad` | `tools/uv.lock` |

- Always `--backend=manifold`: ~135× faster than CGAL on the arm (upper-arm-top 17.5 s → 0.13 s), same geometry.
- `OPENSCADPATH=parts/lib` so parts write `include <BOSL2/std.scad>`, `include <NopSCADlib/vitamins/…>`.
- Rendering is headless: VTK on EGL (the RTX 2060 here; CI without GPU: `VTK_DEFAULT_OPENGL_WINDOW=vtkOSOpenGLRenderWindow`).
  OpenSCAD's PNG export also runs without a display.
- Apt's OpenSCAD 2021.01 may still sit in `/usr/bin`; it has no Manifold backend, so builds fail loudly with it.

## `parts/cad`

```bash
parts/cad build arm [part…]                        # STLs into arm/stl (binary), stats per part; any WARNING fails
parts/cad render arm [part…]                       # PNG sheets into arm/renders/ (gitignored), paths printed
parts/cad render arm upper-arm-top --section y=0 --section z=10   # + 2D cuts
parts/cad render arm -D shoulder=45 -D elbow=90    # -D goes to every file rendered (poses for the assembly)
parts/cad render some.scad | some.stl [-o dir]     # loose files; a module's assembly file renders as assembly
cd parts/tools && uv run pytest -q                 # tool tests (fixtures in tests/fixtures)
```

- **Part sheet** (3 × 3): 2 iso, top, bottom, 4 sides (orthographic, mm scales), and an **x-ray from below** that shows
  only the problem faces through a faint body: the only place internal overhangs are visible. Colours: red =
  overhang > 45°, orange = flat downward face (bridge/roof), green = on the bed. Header: size, volume, max PETG mass
  (solid), bed/overhang/roof areas.
- **Section**: filled 2D cut, 1 mm / 10 mm grid, world axis names kept. Use it for walls, bearing seats, nut slots,
  cable channels: anything inside.
- **Assembly sheet**: OpenSCAD's own preview from 4 directions (gimbal camera), keeps `color()` and `%` ghosts.
- Print classes (`tools/src/cad/mesh.py`): +Z = build direction, lowest Z = bed, 45° counts as fine (teardrops,
  chamfers). Numbers are surface areas, not yet judged: `cad check` (next) adds the rules.

## Working loop (Claude)

After every modelling change: `cad build` → `cad render` → **Read the sheets** and say what each view confirmed;
section through every fit (bearing seat, slot, channel). Numbers can't see a missing hole; the sheet can.

## Rules

- STL only. 3MF files (e.g. `tray/tray.3mf`) are Dev's print projects: never read, write or regenerate them.
- STLs are committed already in print orientation (+Z up = build direction), binary.
- Every dimension in the module's `params.scad`; design rules and fits from `docs/3d-printing.md`.
- `tray/` has no source (Dev's own): change only on request, by editing the STL directly (lid rim gap: `docs/3d-printing.md` → Fits).
- `dispenser/dispenser.stl` is Dev's Onshape box: never modify it. Roller and pusher are built in its coordinates;
  `make_copy.py` writes `ref/` (box, lift, 4023 cut out of it) and the combined copy `dispenser-roller.stl`. The case
  (`case.scad`: `ref/box.stl` + `case_add()` − `box_cuts()`, prints on its back) and the lift (`lift.scad`) are ours
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
- Manifold booleans need watertight input: an open reference mesh (e.g. `dispenser/ref/lift.stl`) silently intersects
  as 0. Check `is_watertight` before trusting an overlap volume.
