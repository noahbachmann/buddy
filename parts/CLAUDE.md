# parts/ – printable parts

Parametric OpenSCAD, one folder per module (`arm/`, later `neck/`, `body/`, `head/`). The CAD environment is being built
in #54: today it covers the toolchain; `cad build/render/check/sweep/slice` and per-module `cad.toml` come next.

## Toolchain

`tools/setup.sh` (idempotent, pinned) installs everything; run it after cloning and when a pin changes.

| What | Where | Pin |
|---|---|---|
| OpenSCAD nightly, extracted AppImage | `~/.local/opt/openscad-<ver>`, linked as `~/.local/bin/openscad` | `OPENSCAD_VERSION` in `setup.sh` |
| BOSL2 | `lib/BOSL2` (submodule) | tag in the submodule commit |
| NopSCADlib | `lib/NopSCADlib` (submodule) | tag in the submodule commit |
| Python (trimesh, manifold3d, pyvista, scipy) | `tools/.venv` via `uv sync` | `tools/uv.lock` |

- Always `--backend=manifold`: ~135× faster than CGAL on the arm (upper-arm-top 17.5 s → 0.13 s), same geometry
  (volumes equal, 0.000 mm max deviation).
- `OPENSCADPATH=parts/lib` so parts write `include <BOSL2/std.scad>`, `include <NopSCADlib/vitamins/…>`.
- Export binary STL (`--export-format=binstl`): ~4× smaller than ASCII in Git.
- Apt's OpenSCAD 2021.01 may still sit in `/usr/bin`; it has no Manifold backend, so builds fail loudly with it.

## Rules

- STL only. 3MF files (e.g. `tray/tray.3mf`) are Dev's print projects: never read, write or regenerate them.
- STLs are committed already in print orientation (+Z up = build direction).
- Every dimension in the module's `params.scad`; design rules and fits from `docs/3d-printing.md`.
- `tray/` has no source (Dev's own): leave it alone.
