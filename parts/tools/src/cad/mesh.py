"""Mesh facts shared by render and check: face classes for printing, basic stats.

Print orientation is the STL as exported: +Z = build direction, lowest Z = bed.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import trimesh

OVERHANG_DEG = 45.0          # max overhang from vertical without support (docs/3d-printing.md)
PETG_DENSITY = 1.27          # g/cm³, solid; real parts lighter (infill)
BED_EPS = 0.01               # mm: faces this close to min Z count as on the bed

# face classes
NORMAL, OVERHANG, ROOF, BED = 0, 1, 2, 3


def load(path: Path) -> trimesh.Trimesh:
    m = trimesh.load(path, force="mesh")
    if not isinstance(m, trimesh.Trimesh) or m.is_empty:
        raise SystemExit(f"{path}: empty or not a mesh")
    return m


def classify(m: trimesh.Trimesh) -> np.ndarray:
    """Per face: NORMAL, OVERHANG (downward, steeper than allowed), ROOF (flat downward: bridge or support),
    BED (flat on the build plate)."""
    nz = m.face_normals[:, 2]
    zmin = m.bounds[0, 2]
    on_bed = np.all(m.vertices[m.faces][:, :, 2] < zmin + BED_EPS, axis=1)
    cls = np.full(len(m.faces), NORMAL, dtype=np.int8)
    # -cos(45°) - tolerance, so an exact 45° chamfer/teardrop passes
    steep = nz < -math.cos(math.radians(OVERHANG_DEG)) - 1e-3
    flat = nz < -0.999
    cls[steep] = OVERHANG
    cls[flat] = ROOF
    cls[on_bed & (nz < -0.999)] = BED
    return cls


@dataclass
class Stats:
    size: tuple[float, float, float]
    volume_cm3: float
    mass_g: float
    watertight: bool
    faces: int
    overhang_mm2: float
    roof_mm2: float
    bed_mm2: float

    def line(self) -> str:
        sx, sy, sz = self.size
        return (f"{sx:.1f} × {sy:.1f} × {sz:.1f} mm · {self.volume_cm3:.1f} cm³ · ≤{self.mass_g:.0f} g PETG · "
                f"bed {self.bed_mm2:.0f} mm² · overhang {self.overhang_mm2:.0f} mm² · roof/bridge {self.roof_mm2:.0f} mm²"
                + ("" if self.watertight else " · NOT WATERTIGHT"))


def stats(m: trimesh.Trimesh, cls: np.ndarray | None = None) -> Stats:
    cls = classify(m) if cls is None else cls
    area = m.area_faces
    vol = abs(m.volume) / 1000.0
    return Stats(
        size=tuple(float(x) for x in m.extents),
        volume_cm3=vol,
        mass_g=vol * PETG_DENSITY,
        watertight=bool(m.is_watertight),
        faces=len(m.faces),
        overhang_mm2=float(area[cls == OVERHANG].sum()),
        roof_mm2=float(area[cls == ROOF].sum()),
        bed_mm2=float(area[cls == BED].sum()),
    )
