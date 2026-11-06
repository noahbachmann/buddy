"""Contact sheets and sections as PNG, made to be read by Claude (and Dev).

- `part_sheet`: 8 orthographic views of one mesh, feature edges, mm scales, print classes coloured
  (overhang red, flat roof/bridge orange, bed contact green), stats in the header.
- `section`: 2D cut through a mesh at axis=value, filled, 1/10 mm grid: walls, seats, slots, channels.
- `assembly_sheet`: OpenSCAD's own preview from 4 directions (keeps colour() and % ghosts, -D poses).
"""

from __future__ import annotations

import os
import tempfile
from pathlib import Path

# headless GPU; CI without a GPU sets vtkOSOpenGLRenderWindow
os.environ.setdefault("VTK_DEFAULT_OPENGL_WINDOW", "vtkEGLRenderWindow")

import matplotlib
import matplotlib.font_manager
import matplotlib.ticker

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pyvista as pv  # noqa: E402
import trimesh  # noqa: E402
from matplotlib.patches import PathPatch  # noqa: E402
from matplotlib.path import Path as MplPath  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

from . import mesh as meshlib, scad  # noqa: E402

pv.OFF_SCREEN = True

NORMAL = meshlib.NORMAL
CLASS_RGB = np.array([
    [190, 200, 212],   # NORMAL
    [220, 40, 40],     # OVERHANG
    [245, 150, 30],    # ROOF / bridge
    [60, 170, 80],     # BED
], dtype=np.uint8)

# (label, direction from model to camera, view-up)
VIEWS = [
    ("iso front-right (+X −Y +Z)", (1, -1, 1), (0, 0, 1)),
    ("iso back-left (−X +Y +Z)", (-1, 1, 1), (0, 0, 1)),
    ("top (from +Z)", (0, 0, 1), (0, 1, 0)),
    ("bottom / bed (from −Z)", (0, 0, -1), (0, 1, 0)),
    ("front (from −Y)", (0, -1, 0), (0, 0, 1)),
    ("back (from +Y)", (0, 1, 0), (0, 0, 1)),
    ("left (from −X)", (-1, 0, 0), (0, 0, 1)),
    ("right (from +X)", (1, 0, 0), (0, 0, 1)),
    ("x-ray from below: problem faces only", (1, -1, -0.7), (0, 0, 1)),
]
CELL = (560, 460)
HEADER = 64


def _font(size: int) -> ImageFont.FreeTypeFont:
    # PIL's default font lacks × ² ³ ≤; matplotlib ships DejaVu Sans
    path = matplotlib.font_manager.findfont(matplotlib.font_manager.FontProperties(family="DejaVu Sans"))
    return ImageFont.truetype(path, size)


def _with_header(img: Image.Image, title: str, sub: str) -> Image.Image:
    out = Image.new("RGB", (img.width, img.height + HEADER), "white")
    out.paste(img, (0, HEADER))
    d = ImageDraw.Draw(out)
    d.text((12, 6), title, fill="black", font=_font(26))
    d.text((12, 38), sub, fill=(60, 60, 60), font=_font(17))
    return out


def _polydata(m: trimesh.Trimesh) -> pv.PolyData:
    faces = np.hstack([np.full((len(m.faces), 1), 3), m.faces]).ravel()
    return pv.PolyData(np.asarray(m.vertices), faces)


def part_sheet(m: trimesh.Trimesh, title: str, out: Path) -> Path:
    cls = meshlib.classify(m)
    st = meshlib.stats(m, cls)
    pd = _polydata(m)
    pd.cell_data["rgb"] = CLASS_RGB[cls]
    edges = pd.extract_feature_edges(feature_angle=35, boundary_edges=True, non_manifold_edges=True,
                                     manifold_edges=False)
    rows, cols = 3, 3
    p = pv.Plotter(shape=(rows, cols), off_screen=True, window_size=(CELL[0] * cols, CELL[1] * rows),
                   border=True, border_color="lightgray")
    p.set_background("white")
    for i, (label, vec, up) in enumerate(VIEWS):
        p.subplot(i // cols, i % cols)
        if label.startswith("x-ray"):
            # whole part faint, downward problem faces opaque: shows overhangs hidden inside
            p.add_mesh(pd, color=CLASS_RGB[NORMAL], opacity=0.12)
            bad = pd.extract_cells(np.flatnonzero((cls == meshlib.OVERHANG) | (cls == meshlib.ROOF)))
            if bad.n_cells:
                p.add_mesh(bad, scalars="rgb", rgb=True, ambient=0.4)
        else:
            p.add_mesh(pd, scalars="rgb", rgb=True, lighting=True, ambient=0.25)
            if edges.n_cells:
                p.add_mesh(edges, color="black", line_width=1.2)
        p.enable_parallel_projection()
        p.view_vector(vec, viewup=up)
        p.reset_camera()
        p.camera.zoom(0.82)
        p.show_bounds(grid=False, location="outer", ticks="outside", font_size=9, fmt="%.0f",
                      xtitle="X", ytitle="Y", ztitle="Z", color="dimgray", minor_ticks=False)
        p.add_text(label, position="upper_left", font_size=9, color="black")
        p.add_axes(line_width=2, labels_off=False, viewport=(0, 0, 0.16, 0.2))
    out.parent.mkdir(parents=True, exist_ok=True)
    img = Image.fromarray(p.screenshot(return_img=True))
    p.close()
    legend = "red = overhang > 45°   orange = flat roof/bridge   green = on bed"
    _with_header(img, title, f"{st.line()}   ({legend})").save(out)
    return out


AXES = {"x": 0, "y": 1, "z": 2}


def section(m: trimesh.Trimesh, axis: str, value: float, title: str, out: Path) -> Path:
    """Cut at axis=value. In-plane axes keep world names: z-cut shows X/Y, x-cut Y/Z, y-cut X/Z."""
    a = AXES[axis]
    keep = [i for i in range(3) if i != a]
    normal = np.zeros(3)
    normal[a] = 1
    origin = np.zeros(3)
    origin[a] = value
    sec = m.section(plane_origin=origin, plane_normal=normal)
    fig, ax = plt.subplots(figsize=(10, 10), dpi=110)
    names = "XYZ"
    if sec is None:
        ax.text(0.5, 0.5, f"nothing at {axis}={value}", ha="center", transform=ax.transAxes)
    else:
        # world → plane: rows pick the two in-plane world axes, third row the cut axis
        to2d = np.eye(4)
        to2d[:3, :3] = np.eye(3)[keep + [a]]
        to2d[2, 3] = -value
        planar, _ = sec.to_2D(to_2D=to2d)
        for poly in planar.polygons_full:
            rings = [np.asarray(poly.exterior.coords)] + [np.asarray(r.coords) for r in poly.interiors]
            verts = np.vstack(rings)
            codes = np.concatenate([[MplPath.MOVETO] + [MplPath.LINETO] * (len(r) - 2) + [MplPath.CLOSEPOLY]
                                    for r in rings])
            ax.add_patch(PathPatch(MplPath(verts, codes), facecolor=(0.55, 0.62, 0.72), edgecolor="black", lw=0.8))
        ax.autoscale_view()
    lo, hi = m.bounds[0][keep], m.bounds[1][keep]
    pad = 0.05 * max(hi - lo)
    ax.set_xlim(lo[0] - pad, hi[0] + pad)
    ax.set_ylim(lo[1] - pad, hi[1] + pad)
    ax.set_aspect("equal")
    ax.set_xlabel(f"{names[keep[0]]} (mm)")
    ax.set_ylabel(f"{names[keep[1]]} (mm)")
    ax.minorticks_on()
    ax.xaxis.set_minor_locator(matplotlib.ticker.MultipleLocator(1))
    ax.yaxis.set_minor_locator(matplotlib.ticker.MultipleLocator(1))
    ax.xaxis.set_major_locator(matplotlib.ticker.MultipleLocator(10))
    ax.yaxis.set_major_locator(matplotlib.ticker.MultipleLocator(10))
    ax.grid(which="major", color="0.6", lw=0.6)
    ax.grid(which="minor", color="0.88", lw=0.4)
    ax.set_axisbelow(True)
    ax.set_title(f"{title} — section {axis.upper()} = {value:g} mm (looking from +{axis.upper()})")
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    return out


ASM_VIEWS = [  # OpenSCAD gimbal rotations
    ("iso front-right", (60, 0, 35)),
    ("front (from −Y)", (90, 0, 0)),
    ("right (from +X)", (90, 0, 90)),
    ("top (from +Z)", (0, 0, 0)),
]


def assembly_sheet(src: Path, defines: dict[str, object], title: str, out: Path) -> Path:
    size = (800, 650)
    tiles = []
    with tempfile.TemporaryDirectory() as tmp:
        for i, (label, rot) in enumerate(ASM_VIEWS):
            png = Path(tmp) / f"{i}.png"
            scad.export_png(src, png, rot, size, defines)
            tile = Image.open(png).convert("RGB")
            ImageDraw.Draw(tile).text((10, 8), label, fill="black", font=_font(18))
            tiles.append(tile)
    sheet = Image.new("RGB", (size[0] * 2, size[1] * 2), "white")
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % 2) * size[0], (i // 2) * size[1]))
    sub = ", ".join(f"{k}={v}" for k, v in defines.items()) or "default pose"
    out.parent.mkdir(parents=True, exist_ok=True)
    _with_header(sheet, title, f"{sub}   (OpenSCAD preview: colours as in the source, % = ghost stand-ins)").save(out)
    return out
